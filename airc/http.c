/*
 * http.c - HTTP client via curl subprocess
 *
 * Plan 9 philosophy: compose with existing tools.
 * Uses curl(1) as a subprocess for HTTPS transport.
 * On Plan 9 proper, replace with dial()/webfs.
 */

#include "airc.h"

/*
 * Build a curl command and execute it.
 * Returns the child's stdout via pipe.
 */
static pid_t
curlexec(char *url, char **hdrs, int nhdrs, char *body, int streaming, int *fdp)
{
	int pfd[2];
	pid_t pid;
	char **argv;
	int argc, i;

	if(pipe(pfd) < 0)
		fatal("pipe: %s", strerror(errno));

	pid = fork();
	if(pid < 0)
		fatal("fork: %s", strerror(errno));

	if(pid == 0){
		/* child: curl process */
		close(pfd[0]);
		dup2(pfd[1], 1);
		dup2(pfd[1], 2);
		if(pfd[1] > 2)
			close(pfd[1]);

		/*
		 * Build argv:
		 * curl -sS [-N] -X POST -d body [-H hdr]... url
		 */
		argc = 6 + nhdrs * 2 + (streaming ? 1 : 0) + 2;
		argv = emalloc(sizeof(char*) * (argc + 1));
		i = 0;
		argv[i++] = "curl";
		argv[i++] = "-sS";
		if(streaming)
			argv[i++] = "-N";	/* unbuffered for SSE */
		argv[i++] = "-X";
		argv[i++] = "POST";
		argv[i++] = "-d";
		argv[i++] = body;
		for(int h = 0; h < nhdrs; h++){
			argv[i++] = "-H";
			argv[i++] = hdrs[h];
		}
		argv[i++] = url;
		argv[i] = NULL;

		execvp("curl", argv);
		fprintf(stderr, "airc: exec curl: %s\n", strerror(errno));
		_exit(1);
	}

	/* parent */
	close(pfd[1]);
	*fdp = pfd[0];
	return pid;
}

/*
 * POST request, read entire response into buf.
 * Returns 0 on success, -1 on error.
 */
int
httppost(char *url, char **hdrs, int nhdrs, char *body, Buf *resp)
{
	int fd, n, status;
	pid_t pid;
	char tmp[4096];

	pid = curlexec(url, hdrs, nhdrs, body, 0, &fd);

	bufreset(resp);
	while((n = read(fd, tmp, sizeof tmp - 1)) > 0)
		bufadd(resp, tmp, n);
	close(fd);

	waitpid(pid, &status, 0);
	if(WIFEXITED(status) && WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

/*
 * POST request with streaming (SSE).
 * Reads line-by-line, calling cb for each data line.
 * SSE format: "data: {...}\n"
 */
int
httpstream(char *url, char **hdrs, int nhdrs, char *body,
	void (*cb)(char*, int, void*), void *aux)
{
	int fd, n, status;
	pid_t pid;
	char tmp[4096];
	Buf line;

	pid = curlexec(url, hdrs, nhdrs, body, 1, &fd);
	bufinit(&line);

	while((n = read(fd, tmp, sizeof tmp - 1)) > 0){
		int i;
		for(i = 0; i < n; i++){
			if(tmp[i] == '\n'){
				char *s = bufstr(&line);
				if(strncmp(s, "data: ", 6) == 0){
					char *data = s + 6;
					int dlen = line.len - 6;
					if(strcmp(data, "[DONE]") != 0)
						cb(data, dlen, aux);
				}
				bufreset(&line);
			}else{
				bufaddc(&line, tmp[i]);
			}
		}
	}
	/* flush last line */
	if(line.len > 0){
		char *s = bufstr(&line);
		if(strncmp(s, "data: ", 6) == 0){
			char *data = s + 6;
			int dlen = line.len - 6;
			if(strcmp(data, "[DONE]") != 0)
				cb(data, dlen, aux);
		}
	}
	buffree(&line);
	close(fd);

	waitpid(pid, &status, 0);
	if(WIFEXITED(status) && WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}
