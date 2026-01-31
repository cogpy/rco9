/* dist.h: Plan 9-style distributed OS commands for Unix rc */

#ifndef DIST_H
#define DIST_H

/*
   Plan 9 namespace binding modes:
   BIND_BEFORE  - new directory appears before old (union mount, priority)
   BIND_AFTER   - new directory appears after old (union mount, fallback)
   BIND_REPLACE - new directory replaces old (default)
*/
enum {
	BIND_REPLACE = 0,
	BIND_BEFORE  = 1,
	BIND_AFTER   = 2,
	BIND_CREATE  = 4
};

/*
   Plan 9 rfork flags (adapted for Unix):
   RFNAMEG  - new namespace group (mount namespace via unshare)
   RFCNAMEG - copy namespace group
   RFENVG   - new environment group (clear env)
   RFCENVG  - copy environment group (already default on Unix)
   RFNOTEG  - new process group (setpgid)
   RFFDG    - new fd group (close all fds)
   RFCFDG   - copy fd group (already default on Unix fork)
   RFPROC   - create new process (fork)
   RFNOWAIT - detach child (double fork)
*/
enum {
	RFNAMEG  = (1 << 0),
	RFCNAMEG = (1 << 1),
	RFENVG   = (1 << 2),
	RFCENVG  = (1 << 3),
	RFNOTEG  = (1 << 4),
	RFFDG    = (1 << 5),
	RFCFDG   = (1 << 6),
	RFPROC   = (1 << 7),
	RFNOWAIT = (1 << 8)
};

/* maximum bind table entries */
#define BIND_MAX 256

/* maximum service entries */
#define SRV_MAX 64

/* default service directory */
#define SRV_DIR "/tmp/rc-srv"

/* Namespace bind table entry */
typedef struct Bind Bind;
struct Bind {
	char *from;		/* source path */
	char *to;		/* mount point */
	int mode;		/* BIND_BEFORE, BIND_AFTER, BIND_REPLACE */
	Bind *n;		/* next entry (for union directories) */
};

/* Service registry entry */
typedef struct Srv Srv;
struct Srv {
	char *name;		/* service name */
	char *path;		/* path to FIFO or socket */
	pid_t pid;		/* owning process (0 if none) */
};

/* dist.c prototypes */
extern void dist_init(void);
extern void dist_cleanup(void);

/* builtin implementations */
extern void b_bind(char **);
extern void b_mount(char **);
extern void b_unmount(char **);
extern void b_ns(char **);
extern void b_cpu(char **);
extern void b_import(char **);
extern void b_srv(char **);
extern void b_rfork(char **);
extern void b_addns(char **);

/* namespace query */
extern char *ns_resolve(char *path);
extern Bind *ns_lookup(char *mountpoint);
extern int ns_count(void);

#endif /* DIST_H */
