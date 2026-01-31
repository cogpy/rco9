/*
 * main.c - airc entry point
 *
 * airc: Plan 9 style AI chat for rc shell
 *
 * Usage:
 *   airc [options] [text...]
 *
 * Modes:
 *   airc "question"          single-shot query
 *   airc                     interactive REPL
 *   airc -e "description"    rc shell assistant
 *   airc -c "request"        code-only output
 *   echo text | airc "prompt" pipe mode
 */

#include "airc.h"

static void
usage(void)
{
	fprintf(stderr,
		"usage: airc [options] [text...]\n"
		"\n"
		"options:\n"
		"  -m model    model spec (e.g. openai:gpt-4o, claude:claude-sonnet-4-20250514)\n"
		"  -r role     activate role (shell, code, or custom name)\n"
		"  -s [name]   use/resume named session\n"
		"  -e          shell assistant mode (generate rc commands)\n"
		"  -c          code-only output (strip explanations)\n"
		"  -f file     include file contents with message\n"
		"  -t temp     temperature (0.0 - 2.0, default 0.7)\n"
		"  -n tokens   max response tokens (default 4096)\n"
		"  -1          disable streaming (wait for complete response)\n"
		"  -h          show this help\n"
		"\n"
		"environment:\n"
		"  OPENAI_API_KEY      OpenAI API key\n"
		"  ANTHROPIC_API_KEY   Anthropic API key\n"
		"  AIRC_LOCAL_URL      local LLM endpoint (Ollama, etc.)\n"
		"  AIRC_MODEL          default model override\n"
		"\n"
		"config: ~/.airc/config, ~/.airc/keys\n"
		"roles: ~/.airc/roles/<name>\n"
		"sessions: ~/.airc/sessions/<name>.json\n"
	);
	exit(1);
}

/* streaming output callback */
static void
printchunk(char *text, int len, void *aux)
{
	(void)len;
	(void)aux;
	fputs(text, stdout);
	fflush(stdout);
}

/*
 * Read all of stdin into a buffer.
 * Returns NULL if stdin is a terminal.
 */
static char*
readstdin(void)
{
	Buf b;
	char tmp[4096];
	int n;

	if(isterm(0))
		return NULL;

	bufinit(&b);
	while((n = read(0, tmp, sizeof tmp - 1)) > 0)
		bufadd(&b, tmp, n);

	if(b.len == 0){
		buffree(&b);
		return NULL;
	}
	return estrdup(bufstr(&b));
}

/*
 * Run a single-shot query.
 */
static void
cmdmode(Config *cfg, Provider *p, char *text, char *sysprompt,
	char *filedata, int codeonly)
{
	Conv *conv;
	Buf input;

	conv = convnew();

	if(sysprompt != NULL)
		convadd(conv, "system", sysprompt);
	else if(codeonly)
		convadd(conv, "system",
			"Respond with only code. No explanations, no markdown "
			"fences, no commentary. Just the raw code.");

	/* build user message with optional file and stdin */
	bufinit(&input);
	if(filedata != NULL){
		bufaddstr(&input, "File contents:\n```\n");
		bufaddstr(&input, filedata);
		bufaddstr(&input, "\n```\n\n");
	}
	bufaddstr(&input, text);

	convadd(conv, "user", bufstr(&input));
	buffree(&input);

	if(cfg->stream)
		aistream(p, conv, cfg, printchunk, NULL);
	else{
		Buf resp;
		bufinit(&resp);
		if(aicomplete(p, conv, cfg, &resp) == 0)
			fputs(bufstr(&resp), stdout);
		buffree(&resp);
	}
	fputs("\n", stdout);

	convfree(conv);
}

/*
 * Shell assistant mode.
 * Translate natural language to rc commands.
 */
static void
shellmode(Config *cfg, Provider *p, char *text)
{
	Conv *conv;
	char *prompt;

	prompt = shellprompt();
	conv = convnew();
	convadd(conv, "system", prompt);
	convadd(conv, "user", text);

	/* generate the command (streaming to show progress) */
	if(aistream(p, conv, cfg, printchunk, NULL) < 0){
		warn("command generation failed");
		free(prompt);
		convfree(conv);
		return;
	}

	/* if interactive, offer to execute */
	if(isterm(0) && conv->tail != NULL
	&& strcmp(conv->tail->role, "assistant") == 0){
		int action = shellconfirm(conv->tail->content);
		(void)action;
	}else{
		fputs("\n", stdout);
	}

	free(prompt);
	convfree(conv);
}

int
main(int argc, char *argv[])
{
	Config *cfg;
	Provider *p;
	Session *sess;
	Role *role;
	char *modelspec, *rolename, *sessname, *filepath;
	char *text, *stdindata, *filedata, *envmodel;
	int mode, opt;
	Buf textbuf;

	modelspec = NULL;
	rolename = NULL;
	sessname = NULL;
	filepath = NULL;
	mode = Mcmd;

	while((opt = getopt(argc, argv, "m:r:s:ecf:t:n:1h")) != -1){
		switch(opt){
		case 'm':
			modelspec = optarg;
			break;
		case 'r':
			rolename = optarg;
			break;
		case 's':
			sessname = optarg;
			break;
		case 'e':
			mode = Mshell;
			break;
		case 'c':
			mode = Mcode;
			break;
		case 'f':
			filepath = optarg;
			break;
		case 't':
			/* handled after config load */
			break;
		case 'n':
			/* handled after config load */
			break;
		case '1':
			/* handled after config load */
			break;
		case 'h':
		default:
			usage();
		}
	}

	/* load configuration */
	cfg = configload(NULL);

	/* apply command-line overrides */
	/* re-parse for numeric options */
	optind = 1;
	while((opt = getopt(argc, argv, "m:r:s:ecf:t:n:1h")) != -1){
		switch(opt){
		case 't':
			cfg->temp = (int)(atof(optarg) * 100);
			break;
		case 'n':
			cfg->maxtoken = atoi(optarg);
			break;
		case '1':
			cfg->stream = False;
			break;
		}
	}

	/* environment variable overrides */
	envmodel = getenv("AIRC_MODEL");
	if(envmodel != NULL && *envmodel != '\0')
		modelspec = envmodel;

	/* resolve provider */
	if(modelspec != NULL)
		p = configprov(cfg, modelspec);
	else
		p = configprov(cfg, NULL);

	if(p == NULL)
		fatal("no API provider configured\n"
			"set OPENAI_API_KEY or ANTHROPIC_API_KEY, or create ~/.airc/keys");

	/* ensure config directory exists */
	mkdirp(cfg->dir);

	/* load role if specified */
	role = NULL;
	if(rolename != NULL){
		role = roleload(cfg, rolename);
		if(role != NULL && role->model != NULL)
			p = configprov(cfg, role->model);
	}

	/* load session if specified */
	sess = NULL;
	if(sessname != NULL){
		sess = sessionload(cfg, sessname);
		if(sess == NULL)
			sess = sessionnew(sessname);
	}

	/* load file if specified */
	filedata = NULL;
	if(filepath != NULL){
		filedata = readfile(filepath);
		if(filedata == NULL)
			fatal("cannot read file: %s", filepath);
	}

	/* read stdin if piped */
	stdindata = NULL;
	if(!isterm(0) && mode != Mrepl)
		stdindata = readstdin();

	/* collect remaining args as text */
	bufinit(&textbuf);
	{
		int i;
		for(i = optind; i < argc; i++){
			if(i > optind)
				bufaddc(&textbuf, ' ');
			bufaddstr(&textbuf, argv[i]);
		}
	}

	/* merge stdin with text */
	if(stdindata != NULL){
		if(textbuf.len > 0){
			/* stdin as context, args as instruction */
			char *combined = smprint("Input:\n```\n%s\n```\n\n%s",
				stdindata, bufstr(&textbuf));
			bufreset(&textbuf);
			bufaddstr(&textbuf, combined);
			free(combined);
		}else{
			bufaddstr(&textbuf, stdindata);
		}
		free(stdindata);
	}

	text = bufstr(&textbuf);

	/* dispatch based on mode */
	if(textbuf.len == 0 && mode == Mcmd){
		/* no text: enter REPL */
		replrun(cfg, p, sess, role);
	}else if(textbuf.len == 0){
		fatal("no input text provided");
	}else{
		switch(mode){
		case Mshell:
			shellmode(cfg, p, text);
			break;
		case Mcode:
			cmdmode(cfg, p, text,
				"Respond with only code. No explanations, "
				"no markdown fences, no commentary.",
				filedata, 1);
			break;
		case Mcmd:
		default:
			cmdmode(cfg, p, text,
				role ? role->prompt : NULL,
				filedata, 0);
			break;
		}
	}

	/* save session if named */
	if(sess != NULL && sessname != NULL)
		sessionsave(cfg, sess);

	/* cleanup */
	buffree(&textbuf);
	free(filedata);
	rolefree(role);
	sessionfree(sess);
	configfree(cfg);
	return 0;
}
