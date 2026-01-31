/*
 * shell.c - rc shell integration
 *
 * Generates rc shell commands from natural language,
 * executes them via the rc shell, and provides
 * shell-aware prompting for the LLM.
 */

#include "airc.h"

/*
 * Detect the current operating system for
 * shell command generation context.
 */
char*
shelldetect(void)
{
#ifdef __APPLE__
	return "macOS";
#elif defined(__linux__)
	return "Linux";
#elif defined(__FreeBSD__)
	return "FreeBSD";
#elif defined(__OpenBSD__)
	return "OpenBSD";
#elif defined(__NetBSD__)
	return "NetBSD";
#else
	return "Unix";
#endif
}

/*
 * Build a system prompt for rc shell command generation.
 * Includes OS detection and rc syntax reference.
 */
char*
shellprompt(void)
{
	return smprint(
		"You are an rc shell command assistant running on %s. "
		"The user describes what they want to do in natural language. "
		"Respond with ONLY the rc shell command(s). No explanations, "
		"no markdown, no code fences. Just the raw command(s).\n\n"
		"rc shell syntax reference:\n"
		"- Variables: x=value  x=(list of values)\n"
		"- Access: $x  $x(2) (1-indexed subscript)\n"
		"- Lists: (a b c) - first class, no word splitting\n"
		"- Command sub: `{cmd}  or  `cmd\n"
		"- Pipes: cmd1 | cmd2\n"
		"- Redirect: cmd > file  cmd >> file  cmd < file\n"
		"- Fd redirect: cmd >[2] file  cmd >[2=1]\n"
		"- Here doc: cmd << 'EOF'\\ntext\\nEOF\n"
		"- If: if(test) cmd  or  if(test){block}\n"
		"- If/else: if(test) cmd; if not cmd\n"
		"- For: for(i in list) cmd\n"
		"- While: while(test) cmd\n"
		"- Switch: switch(val){case pat; cmd}\n"
		"- Functions: fn name {body}\n"
		"- Pattern match: ~ $var pat (returns true/false)\n"
		"- Logical: cmd1 && cmd2  cmd1 || cmd2\n"
		"- Background: cmd &\n"
		"- Status: $status (not $?)\n"
		"- No [[ ]], no (( )), no ${var%%pat}\n"
		"- Quoting: 'literal' (single quotes only, '' for literal ')\n",
		shelldetect());
}

/*
 * Execute a command using rc shell.
 * Tries ./rc first (from this repo), then rc in PATH,
 * then falls back to /bin/sh.
 * Returns the exit status.
 */
int
shellexec(char *cmd)
{
	int status;
	pid_t pid;
	char *shell;

	/* try to find rc */
	if(access("./rc", X_OK) == 0)
		shell = "./rc";
	else if(access("/usr/local/bin/rc", X_OK) == 0)
		shell = "/usr/local/bin/rc";
	else
		shell = "rc";

	pid = fork();
	if(pid < 0){
		warn("fork: %s", strerror(errno));
		return -1;
	}

	if(pid == 0){
		execlp(shell, shell, "-c", cmd, (char*)NULL);
		/* fallback to sh if rc not found */
		execlp("sh", "sh", "-c", cmd, (char*)NULL);
		_exit(127);
	}

	waitpid(pid, &status, 0);
	if(WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}

/*
 * Display a generated command and ask the user
 * what to do with it.
 * Returns: 1 = executed, 0 = cancelled, -1 = revise
 */
int
shellconfirm(char *cmd)
{
	char line[256];
	int c;

	fprintf(stderr, "\n  %s\n\n", cmd);
	fprintf(stderr, "[e]xecute  [r]evise  [c]ancel: ");
	fflush(stderr);

	if(fgets(line, sizeof line, stdin) == NULL)
		return 0;

	c = line[0];
	if(c == 'e' || c == 'E' || c == '1'){
		return shellexec(cmd) == 0 ? 1 : 1;
	}else if(c == 'r' || c == 'R' || c == '2'){
		return -1;
	}
	return 0;
}
