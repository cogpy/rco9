/*
 * repl.c - interactive chat REPL
 *
 * Provides an interactive command loop with
 * dot-commands for mode switching, session
 * management, and configuration.
 */

#include "airc.h"

/* print streaming text to stdout */
static void
printcb(char *text, int len, void *aux)
{
	(void)aux;
	(void)len;
	fputs(text, stdout);
	fflush(stdout);
}

/* history ring buffer */
static char *hist[Maxhist];
static int histn = 0;
static int histpos = 0;

static void
histadd(char *line)
{
	if(histn < Maxhist){
		hist[histn++] = estrdup(line);
	}else{
		free(hist[histpos]);
		hist[histpos] = estrdup(line);
	}
	histpos = (histpos + 1) % Maxhist;
}

static char*
readline_simple(char *prompt)
{
	static char line[Maxline];

	fprintf(stderr, "%s", prompt);
	fflush(stderr);
	if(fgets(line, sizeof line, stdin) == NULL)
		return NULL;

	/* strip trailing newline */
	{
		int n = strlen(line);
		if(n > 0 && line[n-1] == '\n')
			line[n-1] = '\0';
	}
	return line;
}

static void
showhelp(void)
{
	fprintf(stderr,
		"airc REPL commands:\n"
		"  .help              show this help\n"
		"  .model [spec]      show or switch model (e.g. claude:claude-sonnet-4-20250514)\n"
		"  .role <name>       activate a role (shell, code, or custom)\n"
		"  .role              deactivate current role\n"
		"  .session [name]    start or switch session\n"
		"  .save              save current session\n"
		"  .clear             clear conversation history\n"
		"  .info              show current configuration\n"
		"  .shell <text>      generate and offer to execute rc command\n"
		"  .file <path>       include file contents in next message\n"
		"  .exit / .quit      exit REPL\n"
		"  Ctrl-D             exit REPL\n"
		"\n"
		"  Any other input is sent as a chat message.\n"
		"  Use ::: to start/end multi-line input.\n"
	);
}

static void
showinfo(Config *cfg, Provider *p, Session *s, Role *r)
{
	fprintf(stderr, "provider: %s\n", p ? p->name : "(none)");
	fprintf(stderr, "model:    %s\n", p ? p->model : cfg->model);
	fprintf(stderr, "session:  %s\n", s ? s->name : "(none)");
	fprintf(stderr, "role:     %s\n", r ? r->name : "(none)");
	fprintf(stderr, "stream:   %s\n", cfg->stream ? "true" : "false");
	fprintf(stderr, "temp:     %.2f\n", cfg->temp / 100.0);
	fprintf(stderr, "messages: %d\n", s ? s->conv.n : 0);
}

/*
 * Handle shell assistant mode within REPL.
 * Generates rc command and offers to execute.
 */
static void
replshell(Config *cfg, Provider *p, char *text)
{
	Conv *conv;
	Buf resp;
	char *prompt;
	int action;

	prompt = shellprompt();
	conv = convnew();
	convadd(conv, "system", prompt);
	convadd(conv, "user", text);
	free(prompt);

	bufinit(&resp);
	fprintf(stderr, "\n");

	if(aistream(p, conv, cfg, printcb, NULL) < 0){
		warn("shell command generation failed");
		buffree(&resp);
		convfree(conv);
		return;
	}
	fprintf(stdout, "\n");

	/* get the generated command from conversation */
	if(conv->tail != NULL && strcmp(conv->tail->role, "assistant") == 0){
		action = shellconfirm(conv->tail->content);
		if(action == -1){
			/* revise: re-prompt */
			fprintf(stderr, "revision not yet implemented\n");
		}
	}

	buffree(&resp);
	convfree(conv);
}

/*
 * Read file contents and return as a string.
 * Used for .file command to include context.
 */
static char *filebuf = NULL;

static void
loadfile(char *path)
{
	free(filebuf);
	filebuf = readfile(path);
	if(filebuf == NULL)
		warn("cannot read file: %s", path);
	else
		fprintf(stderr, "(loaded %d bytes from %s)\n",
			(int)strlen(filebuf), path);
}

/*
 * Main REPL loop.
 */
void
replrun(Config *cfg, Provider *p, Session *s, Role *r)
{
	char *line, *input;
	Conv *conv;
	int multiline;
	Buf mlbuf;

	if(p == NULL){
		fatal("no provider configured; set OPENAI_API_KEY or ANTHROPIC_API_KEY");
		return;
	}

	/* use session's conversation or create a new one */
	if(s != NULL)
		conv = &s->conv;
	else{
		s = sessionnew(NULL);
		conv = &s->conv;
	}

	/* apply role as system message */
	if(r != NULL && r->prompt != NULL)
		convadd(conv, "system", r->prompt);

	fprintf(stderr, "airc - type .help for commands, Ctrl-D to exit\n");
	multiline = 0;
	bufinit(&mlbuf);

	for(;;){
		if(multiline)
			line = readline_simple("... ");
		else
			line = readline_simple("airc> ");

		if(line == NULL){
			fprintf(stderr, "\n");
			break;
		}

		/* multi-line mode toggle */
		if(strcmp(line, ":::") == 0){
			if(multiline){
				multiline = 0;
				input = estrdup(bufstr(&mlbuf));
				bufreset(&mlbuf);
				goto send;
			}else{
				multiline = 1;
				bufreset(&mlbuf);
				continue;
			}
		}

		if(multiline){
			bufaddstr(&mlbuf, line);
			bufaddc(&mlbuf, '\n');
			continue;
		}

		/* skip empty lines */
		if(*line == '\0')
			continue;

		histadd(line);

		/* dot commands */
		if(line[0] == '.'){
			if(strcmp(line, ".exit") == 0 || strcmp(line, ".quit") == 0)
				break;
			if(strcmp(line, ".help") == 0){
				showhelp();
				continue;
			}
			if(strcmp(line, ".info") == 0){
				showinfo(cfg, p, s, r);
				continue;
			}
			if(strcmp(line, ".clear") == 0){
				/* clear non-system messages */
				Msg *m, *next, *prev;
				prev = NULL;
				for(m = conv->head; m != NULL; m = next){
					next = m->next;
					if(strcmp(m->role, "system") != 0){
						msgfree(m);
						if(prev != NULL)
							prev->next = next;
						else
							conv->head = next;
						conv->n--;
					}else{
						prev = m;
					}
				}
				conv->tail = prev;
				fprintf(stderr, "conversation cleared\n");
				continue;
			}
			if(strcmp(line, ".save") == 0){
				if(sessionsave(cfg, s) == 0)
					fprintf(stderr, "session saved: %s\n", s->name);
				continue;
			}
			if(strcmp(line, ".role") == 0){
				/* deactivate role */
				if(r != NULL){
					rolefree(r);
					r = NULL;
					fprintf(stderr, "role deactivated\n");
				}
				continue;
			}
			if(strncmp(line, ".role ", 6) == 0){
				char *rname = trim(line + 6);
				Role *nr = roleload(cfg, rname);
				if(nr != NULL){
					rolefree(r);
					r = nr;
					/* add/replace system message */
					if(conv->head != NULL
					&& strcmp(conv->head->role, "system") == 0){
						free(conv->head->content);
						conv->head->content = estrdup(r->prompt);
					}else{
						/* prepend system message */
						Msg *sm = msgnew("system", r->prompt);
						sm->next = conv->head;
						conv->head = sm;
						if(conv->tail == NULL)
							conv->tail = sm;
						conv->n++;
					}
					fprintf(stderr, "role: %s\n", r->name);
				}
				continue;
			}
			if(strncmp(line, ".model ", 7) == 0){
				char *spec = trim(line + 7);
				Provider *np = configprov(cfg, spec);
				if(np != NULL){
					p = np;
					fprintf(stderr, "model: %s (%s)\n",
						p->model, p->name);
				}else{
					warn("provider not found: %s", spec);
				}
				continue;
			}
			if(strcmp(line, ".model") == 0){
				fprintf(stderr, "%s:%s\n", p->name, p->model);
				continue;
			}
			if(strncmp(line, ".session ", 8) == 0){
				char *sname = trim(line + 8);
				Session *ns = sessionload(cfg, sname);
				if(ns == NULL)
					ns = sessionnew(sname);
				/* preserve system message */
				if(r != NULL && r->prompt != NULL
				&& (ns->conv.head == NULL
				|| strcmp(ns->conv.head->role, "system") != 0))
					convadd(&ns->conv, "system", r->prompt);
				s = ns;
				conv = &s->conv;
				fprintf(stderr, "session: %s (%d messages)\n",
					s->name, s->conv.n);
				continue;
			}
			if(strncmp(line, ".shell ", 7) == 0){
				replshell(cfg, p, trim(line + 7));
				continue;
			}
			if(strncmp(line, ".file ", 6) == 0){
				loadfile(trim(line + 6));
				continue;
			}
			warn("unknown command: %s", line);
			continue;
		}

		input = estrdup(line);
send:
		/* prepend file contents if loaded */
		if(filebuf != NULL){
			char *combined = smprint("File contents:\n```\n%s\n```\n\n%s",
				filebuf, input);
			free(input);
			input = combined;
			free(filebuf);
			filebuf = NULL;
		}

		convadd(conv, "user", input);
		free(input);

		/* stream response */
		fprintf(stdout, "\n");
		if(aistream(p, conv, cfg, printcb, NULL) < 0)
			warn("request failed");
		fprintf(stdout, "\n\n");
	}

	buffree(&mlbuf);
	free(filebuf);
	filebuf = NULL;
}
