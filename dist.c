/* dist.c: Plan 9-style distributed OS commands for Unix rc
 *
 * Implements the following Plan 9-inspired builtins:
 *
 *   bind [-abc] from to       - bind/overlay directories (namespace)
 *   mount [-abc] [-s spec] srv mp  - mount a 9P or remote filesystem
 *   unmount [from] mountpoint - remove a namespace binding or mount
 *   ns [-r]                   - display current namespace
 *   cpu [-h host] [-u user] cmd - execute command on remote host
 *   import [-abc] host path [mp] - import remote file tree
 *   srv [-r] [name [cmd ...]] - manage named services
 *   rfork [cCeEnNsfF]         - fork with Plan 9-style flags
 *   addns from to             - add a namespace entry (union append)
 *
 * Plan 9 from Bell Labs was a distributed operating system where
 * per-process namespaces, the 9P protocol, and everything-is-a-file
 * semantics allowed transparent distributed computing. These commands
 * bring that philosophy to Unix, using SSH and FUSE as transport.
 */

#include "rc.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#if RC_DIST

#include "dist.h"

/* ========== Namespace Bind Table ========== */

static Bind *bindtab[BIND_MAX];
static int nbinds = 0;

/* ========== Internal Helpers ========== */

/* hash a path to a bind table index */
static unsigned int pathhash(const char *s) {
	unsigned int h = 0;
	while (*s)
		h = h * 31 + (unsigned char)*s++;
	return h % BIND_MAX;
}

/* canonicalize a path (remove trailing slashes, resolve . and ..) */
static char *cleanpath(char *path) {
	char *resolved;
	char buf[4096];
	size_t len;

	if (path == NULL || *path == '\0')
		return ecpy(".");

	/* try realpath first for existing paths */
	resolved = realpath(path, buf);
	if (resolved != NULL)
		return ecpy(buf);

	/* fallback: just clean up the string */
	len = strlen(path);
	resolved = ealloc(len + 1);
	strcpy(resolved, path);

	/* strip trailing slashes (but not root) */
	while (len > 1 && resolved[len - 1] == '/')
		resolved[--len] = '\0';

	return resolved;
}

/* check if a directory exists */
static int isdir(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ensure a directory exists, creating it if mode has BIND_CREATE */
static int ensuredir(const char *path, int mode) {
	if (isdir(path))
		return 0;
	if (mode & BIND_CREATE) {
		if (mkdir(path, 0755) == 0)
			return 0;
		if (errno == EEXIST)
			return 0;
	}
	return -1;
}

/* ensure the service directory exists */
static void ensure_srvdir(void) {
	struct stat st;
	if (stat(SRV_DIR, &st) != 0)
		mkdir(SRV_DIR, 0755);
}

/* find a bind entry for a given mountpoint */
static Bind *find_bind(const char *to) {
	unsigned int h = pathhash(to);
	Bind *b;
	for (b = bindtab[h]; b != NULL; b = b->n)
		if (streq(b->to, (char *)to))
			return b;
	return NULL;
}

/* add a bind entry */
static Bind *add_bind(const char *from, const char *to, int mode) {
	unsigned int h = pathhash(to);
	Bind *b, *existing;

	b = enew(Bind);
	b->from = ecpy((char *)from);
	b->to = ecpy((char *)to);
	b->mode = mode & (BIND_BEFORE | BIND_AFTER | BIND_REPLACE);
	b->n = NULL;

	existing = find_bind(to);
	if (existing != NULL) {
		if (mode & BIND_BEFORE) {
			/* insert before existing */
			b->n = bindtab[h];
			bindtab[h] = b;
		} else if (mode & BIND_AFTER) {
			/* insert after existing, at end */
			Bind *p = existing;
			while (p->n != NULL)
				p = p->n;
			p->n = b;
		} else {
			/* replace: remove old, insert new */
			Bind **pp = &bindtab[h];
			while (*pp != NULL) {
				if (streq((*pp)->to, (char *)to)) {
					Bind *old = *pp;
					*pp = old->n;
					efree(old->from);
					efree(old->to);
					efree(old);
				} else {
					pp = &(*pp)->n;
				}
			}
			b->n = bindtab[h];
			bindtab[h] = b;
		}
	} else {
		b->n = bindtab[h];
		bindtab[h] = b;
	}
	nbinds++;
	return b;
}

/* remove a bind entry */
static int remove_bind(const char *from, const char *to) {
	unsigned int h = pathhash(to);
	Bind **pp = &bindtab[h];
	int found = 0;

	while (*pp != NULL) {
		Bind *b = *pp;
		if (streq(b->to, (char *)to) &&
		    (from == NULL || streq(b->from, (char *)from))) {
			*pp = b->n;
			efree(b->from);
			efree(b->to);
			efree(b);
			nbinds--;
			found = 1;
			if (from != NULL)
				break; /* only remove specific binding */
		} else {
			pp = &(*pp)->n;
		}
	}
	return found;
}

/* execute a command and wait for it (helper for mount/import/cpu) */
static int run_cmd(char **argv) {
	int stat;
	pid_t pid;

	pid = rc_fork();
	if (pid == 0) {
		setsigdefaults(FALSE);
		execvp(argv[0], argv);
		uerror(argv[0]);
		_exit(127);
	}
	if (pid < 0) {
		uerror("fork");
		return -1;
	}
	rc_wait4(pid, &stat, TRUE);
	return stat;
}

/* ========== bind [-abcr] from to ========== */

/*
   Plan 9 bind overlays one directory onto another in the process's
   namespace. Unlike Unix mount, this is per-process and doesn't
   require privileges.

   -a  union mount: from appears after to (fallback)
   -b  union mount: from appears before to (priority)
   -c  create the mount point if it doesn't exist
   (default is replace)

   The bind table is maintained in-shell and exported to children
   via the $ns variable.
*/

extern void b_bind(char **av) {
	int mode = BIND_REPLACE;
	char *from, *to;
	char *cfrom, *cto;

	for (++av; *av != NULL && **av == '-'; av++) {
		char *f = *av + 1;
		while (*f) {
			switch (*f++) {
			case 'a': mode = (mode & ~BIND_BEFORE) | BIND_AFTER; break;
			case 'b': mode = (mode & ~BIND_AFTER) | BIND_BEFORE; break;
			case 'c': mode |= BIND_CREATE; break;
			default:
				fprint(2, RC "bind: unknown flag -%c\n", f[-1]);
				set(FALSE);
				return;
			}
		}
	}
	if (av[0] == NULL || av[1] == NULL) {
		fprint(2, RC "usage: bind [-abc] from to\n");
		set(FALSE);
		return;
	}
	if (av[2] != NULL) {
		fprint(2, RC "bind: too many arguments\n");
		set(FALSE);
		return;
	}
	from = av[0];
	to = av[1];

	/* validate source exists */
	if (!isdir(from)) {
		struct stat st;
		if (stat(from, &st) != 0) {
			fprint(2, RC "bind: %s: %s\n", from, strerror(errno));
			set(FALSE);
			return;
		}
	}

	/* validate or create mount point */
	if (ensuredir(to, mode) != 0 && !isdir(to)) {
		/* for non-directory binds (files), just check existence */
		struct stat st;
		if (stat(to, &st) != 0) {
			fprint(2, RC "bind: %s: %s\n", to, strerror(errno));
			set(FALSE);
			return;
		}
	}

	cfrom = cleanpath(from);
	cto = cleanpath(to);
	add_bind(cfrom, cto, mode);

	/* export namespace to environment */
	varassign("ns_bind_last", word(nprint("%s %s", cfrom, cto), NULL), FALSE);

	if (dashex)
		fprint(2, "bind %s%s %s %s\n",
		       (mode & BIND_BEFORE) ? "-b " :
		       (mode & BIND_AFTER)  ? "-a " : "",
		       cfrom, cto, "");

	efree(cfrom);
	efree(cto);
	set(TRUE);
}

/* ========== mount [-abc] [-s spec] address mountpoint ========== */

/*
   Plan 9 mount attaches a 9P server to a point in the namespace.
   On Unix, we support:
   - 9P via 9pfuse or v9fs
   - SSH-based remote mount via sshfs
   - NFS mount (if available)
   - Local mount --bind (if root)
*/

extern void b_mount(char **av) {
	int mode = BIND_REPLACE;
	char *spec = NULL;
	char *addr, *mountpoint;
	int stat;
	char *mountcmd[16];
	int argc = 0;

	for (++av; *av != NULL && **av == '-'; av++) {
		char *f = *av + 1;
		if (*f == 's' && f[1] == '\0') {
			spec = *++av;
			if (spec == NULL) {
				fprint(2, RC "mount: -s requires argument\n");
				set(FALSE);
				return;
			}
			continue;
		}
		while (*f) {
			switch (*f++) {
			case 'a': mode = (mode & ~BIND_BEFORE) | BIND_AFTER; break;
			case 'b': mode = (mode & ~BIND_AFTER) | BIND_BEFORE; break;
			case 'c': mode |= BIND_CREATE; break;
			case 'n': break; /* no-auth, ignored on Unix */
			default:
				fprint(2, RC "mount: unknown flag -%c\n", f[-1]);
				set(FALSE);
				return;
			}
		}
	}
	if (av[0] == NULL || av[1] == NULL) {
		fprint(2, RC "usage: mount [-abc] [-s spec] address mountpoint\n");
		set(FALSE);
		return;
	}
	addr = av[0];
	mountpoint = av[1];

	if (ensuredir(mountpoint, mode | BIND_CREATE) != 0) {
		fprint(2, RC "mount: cannot create %s: %s\n", mountpoint, strerror(errno));
		set(FALSE);
		return;
	}

	/* detect mount method from address format */
	if (strchr(addr, ':') != NULL && strchr(addr, '/') != NULL) {
		/* host:/path format -> try sshfs first, then NFS */
		char *colon = strchr(addr, ':');
		char host[256];
		size_t hlen = (size_t)(colon - addr);

		if (hlen >= sizeof(host))
			hlen = sizeof(host) - 1;
		memcpy(host, addr, hlen);
		host[hlen] = '\0';

		/* try sshfs */
		argc = 0;
		mountcmd[argc++] = "sshfs";
		mountcmd[argc++] = addr;
		mountcmd[argc++] = mountpoint;
		mountcmd[argc++] = "-o";
		mountcmd[argc++] = "reconnect,ServerAliveInterval=15";
		if (spec != NULL) {
			mountcmd[argc++] = "-o";
			mountcmd[argc++] = spec;
		}
		mountcmd[argc] = NULL;

		stat = run_cmd(mountcmd);
		if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0) {
			char *cfrom = cleanpath(addr);
			char *cto = cleanpath(mountpoint);
			add_bind(cfrom, cto, mode);
			efree(cfrom);
			efree(cto);
			set(TRUE);
			return;
		}
		fprint(2, RC "mount: sshfs failed, trying mount(8)\n");
	}

	/* try system mount for 9P or local */
	argc = 0;
	mountcmd[argc++] = "mount";
	if (spec != NULL) {
		mountcmd[argc++] = "-t";
		mountcmd[argc++] = spec;
	}
	mountcmd[argc++] = addr;
	mountcmd[argc++] = mountpoint;
	mountcmd[argc] = NULL;

	stat = run_cmd(mountcmd);
	if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0) {
		char *cfrom = cleanpath(addr);
		char *cto = cleanpath(mountpoint);
		add_bind(cfrom, cto, mode);
		efree(cfrom);
		efree(cto);
		set(TRUE);
		return;
	}

	fprint(2, RC "mount: failed to mount %s on %s\n", addr, mountpoint);
	set(FALSE);
}

/* ========== unmount [from] mountpoint ========== */

/*
   Remove a namespace binding. If 'from' is specified, only remove
   that specific binding. Otherwise remove all bindings at mountpoint.
   Also attempts system unmount for actual mounts.
*/

extern void b_unmount(char **av) {
	char *from = NULL, *mp;
	int found;

	av++;
	if (*av == NULL) {
		fprint(2, RC "usage: unmount [from] mountpoint\n");
		set(FALSE);
		return;
	}
	if (av[1] != NULL) {
		from = av[0];
		mp = av[1];
		if (av[2] != NULL) {
			fprint(2, RC "unmount: too many arguments\n");
			set(FALSE);
			return;
		}
	} else {
		mp = av[0];
	}

	/* try to remove from our bind table */
	found = remove_bind(from, mp);

	/* also try system unmount */
	{
		char *umountcmd[4];
		int stat;
		umountcmd[0] = "umount";
		umountcmd[1] = mp;
		umountcmd[2] = NULL;
		stat = run_cmd(umountcmd);
		if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0)
			found = 1;

		/* try fusermount as fallback */
		if (!found) {
			umountcmd[0] = "fusermount";
			umountcmd[1] = "-u";
			umountcmd[2] = mp;
			umountcmd[3] = NULL;
			stat = run_cmd(umountcmd);
			if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0)
				found = 1;
		}
	}

	if (!found) {
		fprint(2, RC "unmount: %s: not mounted\n", mp);
		set(FALSE);
		return;
	}

	if (dashex)
		fprint(2, "unmount %s%s\n", from ? from : "", mp);
	set(TRUE);
}

/* ========== ns [-r] ========== */

/*
   Display the current namespace. Shows all bind table entries
   and active mounts. With -r, output is in a form that can be
   used to recreate the namespace (rc script format).
*/

extern void b_ns(char **av) {
	int recreate = 0;
	int i, count = 0;
	Bind *b;

	for (++av; *av != NULL && **av == '-'; av++) {
		char *f = *av + 1;
		while (*f) {
			switch (*f++) {
			case 'r': recreate = 1; break;
			default:
				fprint(2, RC "ns: unknown flag -%c\n", f[-1]);
				set(FALSE);
				return;
			}
		}
	}
	if (*av != NULL) {
		fprint(2, RC "usage: ns [-r]\n");
		set(FALSE);
		return;
	}

	/* print bind table entries */
	for (i = 0; i < BIND_MAX; i++) {
		for (b = bindtab[i]; b != NULL; b = b->n) {
			if (recreate) {
				fprint(1, "bind %s%S %S\n",
				       (b->mode & BIND_BEFORE) ? "-b " :
				       (b->mode & BIND_AFTER)  ? "-a " : "",
				       b->from, b->to);
			} else {
				char *mstr;
				if (b->mode & BIND_BEFORE)
					mstr = "before";
				else if (b->mode & BIND_AFTER)
					mstr = "after";
				else
					mstr = "replace";
				fprint(1, "%S\t%S\t(%s)\n", b->from, b->to, mstr);
			}
			count++;
		}
	}

	/* also show system mounts from /proc if no bind entries */
	if (count == 0 && !recreate) {
		int fd = open("/proc/mounts", O_RDONLY);
		if (fd >= 0) {
			char buf[8192];
			int n;
			fprint(1, "# system mounts:\n");
			while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
				buf[n] = '\0';
				writeall(1, buf, n);
			}
			close(fd);
		} else {
			/* try mount command as fallback */
			char *mcmd[2];
			mcmd[0] = "mount";
			mcmd[1] = NULL;
			run_cmd(mcmd);
		}
	}
	set(TRUE);
}

/* ========== cpu [-h host] [-u user] [-A] cmd [args...] ========== */

/*
   Execute a command on a remote host, in the spirit of Plan 9's
   cpu command. This implementation uses SSH as the transport layer
   but provides Plan 9-like semantics:

   - Exports the current $path to the remote
   - Exports shell functions to the remote
   - Sets up the remote environment to match local
   - Returns the remote exit status

   -h host   specify remote host (or set $cpu variable)
   -u user   specify remote user
   -A        forward SSH agent
*/

extern void b_cpu(char **av) {
	char *host = NULL, *user = NULL;
	int forward_agent = 0;
	char *sshcmd[64];
	int argc = 0;
	int stat;
	List *s;
	char *remote_cmd;
	char envbuf[8192];
	int envlen = 0;

	for (++av; *av != NULL && **av == '-'; av++) {
		char *f = *av + 1;
		switch (*f) {
		case 'h':
			if (f[1] != '\0')
				host = f + 1;
			else if (*++av != NULL)
				host = *av;
			else {
				fprint(2, RC "cpu: -h requires argument\n");
				set(FALSE);
				return;
			}
			break;
		case 'u':
			if (f[1] != '\0')
				user = f + 1;
			else if (*++av != NULL)
				user = *av;
			else {
				fprint(2, RC "cpu: -u requires argument\n");
				set(FALSE);
				return;
			}
			break;
		case 'A':
			forward_agent = 1;
			break;
		default:
			fprint(2, RC "cpu: unknown flag -%c\n", *f);
			set(FALSE);
			return;
		}
	}

	/* host from argument or $cpu variable */
	if (host == NULL) {
		s = varlookup("cpu");
		if (s != NULL)
			host = s->w;
	}
	if (host == NULL) {
		fprint(2, RC "cpu: no host specified (use -h or set $cpu)\n");
		set(FALSE);
		return;
	}

	if (*av == NULL) {
		fprint(2, RC "usage: cpu [-h host] [-u user] [-A] cmd [args...]\n");
		set(FALSE);
		return;
	}

	/* build the remote command with environment export */
	envlen = 0;

	/* export $path as PATH */
	s = varlookup("path");
	if (s != NULL) {
		envlen += snprintf(envbuf + envlen, sizeof(envbuf) - envlen,
				   "PATH=");
		while (s != NULL) {
			envlen += snprintf(envbuf + envlen, sizeof(envbuf) - envlen,
					   "%s%s", s->w, s->n ? ":" : "");
			s = s->n;
		}
		envlen += snprintf(envbuf + envlen, sizeof(envbuf) - envlen, "; ");
	}

	/* build the actual command string */
	{
		char cmdbuf[4096];
		int cmdlen = 0;
		char **p;

		/* prepend environment */
		cmdlen = snprintf(cmdbuf, sizeof(cmdbuf), "%s", envbuf);

		/* append the command and args */
		for (p = av; *p != NULL; p++) {
			if (p != av)
				cmdlen += snprintf(cmdbuf + cmdlen,
						   sizeof(cmdbuf) - cmdlen, " ");
			/* quote args with spaces */
			if (strchr(*p, ' ') != NULL || strchr(*p, '\t') != NULL)
				cmdlen += snprintf(cmdbuf + cmdlen,
						   sizeof(cmdbuf) - cmdlen,
						   "'%s'", *p);
			else
				cmdlen += snprintf(cmdbuf + cmdlen,
						   sizeof(cmdbuf) - cmdlen,
						   "%s", *p);
		}
		remote_cmd = nalloc(cmdlen + 1);
		strcpy(remote_cmd, cmdbuf);
	}

	/* build ssh command */
	argc = 0;
	sshcmd[argc++] = "ssh";
	if (forward_agent)
		sshcmd[argc++] = "-A";
	sshcmd[argc++] = "-o";
	sshcmd[argc++] = "BatchMode=yes";
	if (user != NULL) {
		sshcmd[argc++] = "-l";
		sshcmd[argc++] = user;
	}
	sshcmd[argc++] = host;
	sshcmd[argc++] = remote_cmd;
	sshcmd[argc] = NULL;

	if (dashex) {
		int i;
		fprint(2, "cpu:");
		for (i = 0; sshcmd[i] != NULL; i++)
			fprint(2, " %s", sshcmd[i]);
		fprint(2, "\n");
	}

	stat = run_cmd(sshcmd);
	setstatus(-1, stat);
	sigchk();
}

/* ========== import [-abc] host path [mountpoint] ========== */

/*
   Import a remote file tree into the local namespace, inspired
   by Plan 9's import command. Uses sshfs for the actual mount.

   -a  union mount after (fallback)
   -b  union mount before (priority)
   -c  create mount point if needed

   If mountpoint is omitted, uses the same path locally.
*/

extern void b_import(char **av) {
	int mode = BIND_REPLACE;
	char *host, *path, *mp;
	char addr[4096];
	char *mountcmd[16];
	int argc, stat;

	for (++av; *av != NULL && **av == '-'; av++) {
		char *f = *av + 1;
		while (*f) {
			switch (*f++) {
			case 'a': mode = (mode & ~BIND_BEFORE) | BIND_AFTER; break;
			case 'b': mode = (mode & ~BIND_AFTER) | BIND_BEFORE; break;
			case 'c': mode |= BIND_CREATE; break;
			default:
				fprint(2, RC "import: unknown flag -%c\n", f[-1]);
				set(FALSE);
				return;
			}
		}
	}

	if (*av == NULL || av[1] == NULL) {
		fprint(2, RC "usage: import [-abc] host path [mountpoint]\n");
		set(FALSE);
		return;
	}
	host = av[0];
	path = av[1];
	mp = (av[2] != NULL) ? av[2] : path;

	/* create mount point if needed */
	if (ensuredir(mp, mode | BIND_CREATE) != 0) {
		fprint(2, RC "import: cannot create %s: %s\n", mp, strerror(errno));
		set(FALSE);
		return;
	}

	/* construct sshfs address */
	snprintf(addr, sizeof(addr), "%s:%s", host, path);

	if (dashex)
		fprint(2, "import %s %s -> %s\n", host, path, mp);

	/* try sshfs */
	argc = 0;
	mountcmd[argc++] = "sshfs";
	mountcmd[argc++] = addr;
	mountcmd[argc++] = mp;
	mountcmd[argc++] = "-o";
	mountcmd[argc++] = "reconnect,ServerAliveInterval=15,follow_symlinks";
	mountcmd[argc] = NULL;

	stat = run_cmd(mountcmd);
	if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0) {
		char *cfrom = cleanpath(addr);
		char *cto = cleanpath(mp);
		add_bind(cfrom, cto, mode);
		efree(cfrom);
		efree(cto);
		set(TRUE);
		return;
	}

	/* try 9pfuse as a fallback for 9P servers */
	argc = 0;
	mountcmd[argc++] = "9pfuse";
	mountcmd[argc++] = addr;
	mountcmd[argc++] = mp;
	mountcmd[argc] = NULL;

	stat = run_cmd(mountcmd);
	if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0) {
		char *cfrom = cleanpath(addr);
		char *cto = cleanpath(mp);
		add_bind(cfrom, cto, mode);
		efree(cfrom);
		efree(cto);
		set(TRUE);
		return;
	}

	fprint(2, RC "import: could not import %s from %s\n", path, host);
	set(FALSE);
}

/* ========== srv [-r] [name [cmd [args...]]] ========== */

/*
   Post or list named services, inspired by Plan 9's /srv.
   Services are named pipes (FIFOs) in a well-known directory
   that allow processes to rendezvous.

   srv              - list all services (scans srv directory)
   srv name         - open/connect to named service
   srv name cmd ... - create service running cmd, post as name
   srv -r name      - remove a service

   State is on the filesystem (SRV_DIR), not in memory, so services
   persist across redirections and subshells.
*/

extern void b_srv(char **av) {
	int remove = 0;
	char *name;
	char srvpath[4096];

	for (++av; *av != NULL && **av == '-'; av++) {
		char *f = *av + 1;
		while (*f) {
			switch (*f++) {
			case 'r': remove = 1; break;
			default:
				fprint(2, RC "srv: unknown flag -%c\n", f[-1]);
				set(FALSE);
				return;
			}
		}
	}

	ensure_srvdir();

	/* list services by scanning the srv directory */
	if (*av == NULL && !remove) {
		DIR *d;
		struct dirent *ent;
		int found = 0;

		d = opendir(SRV_DIR);
		if (d != NULL) {
			while ((ent = readdir(d)) != NULL) {
				struct stat st;
				if (ent->d_name[0] == '.')
					continue;
				snprintf(srvpath, sizeof(srvpath), "%s/%s",
					 SRV_DIR, ent->d_name);
				if (stat(srvpath, &st) == 0) {
					char *type;
					if (S_ISFIFO(st.st_mode))
						type = "fifo";
					else if (S_ISSOCK(st.st_mode))
						type = "sock";
					else
						type = "file";
					fprint(1, "%s\t%s\t(%s)\n",
					       ent->d_name, srvpath, type);
					found = 1;
				}
			}
			closedir(d);
		}
		if (!found)
			fprint(1, "# no services (srv dir: %s)\n", SRV_DIR);
		set(TRUE);
		return;
	}

	if (*av == NULL) {
		fprint(2, RC "usage: srv [-r] [name [cmd ...]]\n");
		set(FALSE);
		return;
	}

	name = *av++;

	snprintf(srvpath, sizeof(srvpath), "%s/%s", SRV_DIR, name);

	/* remove a service */
	if (remove) {
		struct stat st;
		if (stat(srvpath, &st) != 0) {
			fprint(2, RC "srv: %s: not found\n", name);
			set(FALSE);
			return;
		}
		unlink(srvpath);
		if (dashex)
			fprint(2, "srv: removed %s\n", name);
		set(TRUE);
		return;
	}

	/* connect to existing service */
	if (*av == NULL) {
		struct stat st;
		if (stat(srvpath, &st) == 0) {
			/* export the service path as $srv_<name> */
			char varname[256];
			snprintf(varname, sizeof(varname), "srv_%s", name);
			varassign(varname, word(srvpath, NULL), FALSE);
			fprint(1, "%s\n", srvpath);
			set(TRUE);
		} else {
			fprint(2, RC "srv: %s: not found\n", name);
			set(FALSE);
		}
		return;
	}

	/* create new service: make FIFO and run command */

	/* create FIFO */
	unlink(srvpath); /* remove old one if exists */
	if (mkfifo(srvpath, 0666) < 0) {
		fprint(2, RC "srv: cannot create %s: %s\n", srvpath, strerror(errno));
		set(FALSE);
		return;
	}

	/* fork to run the command with stdin/stdout connected to FIFO */
	{
		pid_t pid = rc_fork();
		if (pid == 0) {
			int fd;
			setsigdefaults(FALSE);
			fd = open(srvpath, O_RDWR);
			if (fd < 0)
				_exit(1);
			if (fd != 0) { dup2(fd, 0); }
			if (fd != 1) { dup2(fd, 1); }
			if (fd > 1) close(fd);
			execvp(*av, av);
			uerror(*av);
			_exit(127);
		}
		if (pid < 0) {
			uerror("fork");
			unlink(srvpath);
			set(FALSE);
			return;
		}

		if (dashex)
			fprint(2, "srv: %s -> %s (pid %d)\n", name, srvpath, pid);

		varassign("apid", word(nprint("%d", pid), NULL), FALSE);
		set(TRUE);
	}
}

/* ========== rfork [cCeEnNsfF] ========== */

/*
   Modify the current process's namespace and attributes using
   Plan 9-style rfork flags. Unlike Plan 9's rfork which always
   creates a new process, this applies flags to the current
   process (use @{...} for subshell semantics).

   Flags:
     c - new cgroup/namespace (mount namespace, Linux only)
     C - copy namespace (no-op on Unix, already default)
     e - new environment (clear all env vars)
     E - copy environment (no-op on Unix, already default)
     n - new namespace group (mount namespace via unshare)
     N - copy namespace group (no-op on Unix)
     s - new process group (setpgid)
     f - new fd group (close non-std fds)
     F - copy fd group (no-op on Unix, already default)

   Without arguments, rfork creates a new process group.
*/

extern void b_rfork(char **av) {
	int flags = 0;
	char *f;

	if (*++av != NULL) {
		f = *av;
		while (*f) {
			switch (*f++) {
			case 'c': case 'n':
				flags |= RFNAMEG; break;
			case 'C': case 'N':
				flags |= RFCNAMEG; break;
			case 'e':
				flags |= RFENVG; break;
			case 'E':
				flags |= RFCENVG; break;
			case 's':
				flags |= RFNOTEG; break;
			case 'f':
				flags |= RFFDG; break;
			case 'F':
				flags |= RFCFDG; break;
			default:
				fprint(2, RC "rfork: unknown flag %c\n", f[-1]);
				set(FALSE);
				return;
			}
		}
	}

	/* default: just new process group */
	if (flags == 0)
		flags = RFNOTEG;

	/* apply flags to the current process */

	/* new process group */
	if (flags & RFNOTEG) {
		if (setpgid(0, getpid()) < 0 && errno != EPERM) {
			/* EPERM is normal if already a group leader */
			if (dashex)
				fprint(2, "rfork: setpgid: %s\n", strerror(errno));
		}
	}

	/* new mount namespace (Linux only) */
#ifdef __linux__
	if (flags & RFNAMEG) {
		extern int unshare(int);
		if (unshare(0x00020000) < 0) { /* CLONE_NEWNS */
			fprint(2, RC "rfork: unshare(CLONE_NEWNS): %s\n",
			       strerror(errno));
			set(FALSE);
			return;
		}
	}
#else
	if (flags & RFNAMEG) {
		fprint(2, RC "rfork: mount namespace not supported on this platform\n");
		set(FALSE);
		return;
	}
#endif

	/* clear environment */
	if (flags & RFENVG) {
		extern char **environ;
		static char *empty_env[] = { NULL };
		List *defpath;
		environ = empty_env;
		/* also reset rc's internal path */
		defpath = append(word("/usr/local/bin", NULL),
			  append(word("/usr/bin", NULL),
			  word("/bin", NULL)));
		varassign("path", defpath, FALSE);
		alias("path", varlookup("path"), FALSE);
	}

	/* close non-standard file descriptors */
	if (flags & RFFDG) {
		int fd;
		for (fd = 3; fd < 256; fd++)
			close(fd);
	}

	set(TRUE);
}

/* ========== addns from to ========== */

/*
   Convenience command to add a union namespace entry.
   Equivalent to: bind -a from to
   Primarily for use in namespace setup scripts.
*/

extern void b_addns(char **av) {
	char *args[5];
	args[0] = "bind";
	args[1] = "-a";
	if (av[1] == NULL || av[2] == NULL) {
		fprint(2, RC "usage: addns from to\n");
		set(FALSE);
		return;
	}
	args[2] = av[1];
	args[3] = av[2];
	args[4] = NULL;
	b_bind(args);
}

/* ========== Namespace Resolution ========== */

/*
   Resolve a path through the bind table. Used internally
   to translate paths through the shell's namespace.
*/

extern char *ns_resolve(char *path) {
	unsigned int h;
	Bind *b;
	char *clean;

	if (path == NULL)
		return NULL;

	clean = cleanpath(path);
	h = pathhash(clean);

	for (b = bindtab[h]; b != NULL; b = b->n) {
		if (streq(b->to, clean)) {
			efree(clean);
			return b->from;
		}
	}
	efree(clean);
	return path; /* no binding, return original */
}

extern Bind *ns_lookup(char *mountpoint) {
	return find_bind(mountpoint);
}

extern int ns_count(void) {
	return nbinds;
}

/* ========== Init/Cleanup ========== */

extern void dist_init(void) {
	memzero(bindtab, sizeof(bindtab));
	nbinds = 0;
}

extern void dist_cleanup(void) {
	int i;
	Bind *b, *next;

	/* free bind table */
	for (i = 0; i < BIND_MAX; i++) {
		for (b = bindtab[i]; b != NULL; b = next) {
			next = b->n;
			efree(b->from);
			efree(b->to);
			efree(b);
		}
		bindtab[i] = NULL;
	}
	nbinds = 0;
}

#endif /* RC_DIST */
