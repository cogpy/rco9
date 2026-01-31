/*
 * config.c - configuration loading
 *
 * Reads ~/.airc/config in a simple line-based format:
 *   key value
 *   # comments
 *
 * Provider keys in ~/.airc/keys:
 *   openai sk-xxx gpt-4o
 *   claude sk-ant-xxx claude-sonnet-4-20250514
 */

#include "airc.h"

static char*
cfgdir(void)
{
	char *home, *dir;

	home = homedir();
	dir = pathjoin(home, ".airc");
	return dir;
}

char*
configdir(void)
{
	return cfgdir();
}

static void
loadkeys(Config *cfg)
{
	char *path, *data, *line, *next;
	char *name, *key, *model;

	path = pathjoin(cfg->dir, "keys");
	data = readfile(path);
	free(path);
	if(data == NULL)
		return;

	for(line = data; line != NULL && *line != '\0'; line = next){
		next = strchr(line, '\n');
		if(next != NULL)
			*next++ = '\0';

		/* skip comments and blank lines */
		line = trim(line);
		if(*line == '\0' || *line == '#')
			continue;

		/* format: provider_name api_key [model] */
		name = strtok(line, " \t");
		key = strtok(NULL, " \t");
		model = strtok(NULL, " \t");
		if(name == NULL || key == NULL)
			continue;

		if(strcmp(name, "openai") == 0){
			Provider *p = provnew(Popenai, "openai", key,
				model ? model : "gpt-4o");
			cfg->provs = erealloc(cfg->provs,
				sizeof(Provider*) * (cfg->nprov + 1));
			cfg->provs[cfg->nprov++] = p;
		}else if(strcmp(name, "claude") == 0){
			Provider *p = provnew(Pclaude, "claude", key,
				model ? model : "claude-sonnet-4-20250514");
			cfg->provs = erealloc(cfg->provs,
				sizeof(Provider*) * (cfg->nprov + 1));
			cfg->provs[cfg->nprov++] = p;
		}else if(strcmp(name, "local") == 0){
			Provider *p = provnew(Plocal, name, key,
				model ? model : "llama3");
			cfg->provs = erealloc(cfg->provs,
				sizeof(Provider*) * (cfg->nprov + 1));
			cfg->provs[cfg->nprov++] = p;
		}
	}
	free(data);
}

Config*
configload(char *path)
{
	Config *cfg;
	char *data, *line, *next;
	char *key, *val;

	cfg = emalloc(sizeof *cfg);
	cfg->dir = cfgdir();
	cfg->model = estrdup("openai:gpt-4o");
	cfg->stream = True;
	cfg->temp = 70;		/* 0.7 */
	cfg->maxtoken = 4096;
	cfg->provs = NULL;
	cfg->nprov = 0;
	cfg->curprov = NULL;

	/* load config file */
	if(path == NULL)
		path = pathjoin(cfg->dir, "config");
	data = readfile(path);

	if(data != NULL){
		for(line = data; line != NULL && *line != '\0'; line = next){
			next = strchr(line, '\n');
			if(next != NULL)
				*next++ = '\0';

			line = trim(line);
			if(*line == '\0' || *line == '#')
				continue;

			key = line;
			val = strchr(line, ' ');
			if(val == NULL)
				val = strchr(line, '\t');
			if(val == NULL)
				continue;
			*val++ = '\0';
			val = trim(val);

			if(strcmp(key, "model") == 0){
				free(cfg->model);
				cfg->model = estrdup(val);
			}else if(strcmp(key, "stream") == 0){
				cfg->stream = (strcmp(val, "true") == 0);
			}else if(strcmp(key, "temperature") == 0){
				cfg->temp = (int)(atof(val) * 100);
			}else if(strcmp(key, "max_tokens") == 0){
				cfg->maxtoken = atoi(val);
			}
		}
		free(data);
	}

	/* load provider keys */
	loadkeys(cfg);

	/* check env vars as fallback */
	if(cfg->nprov == 0){
		char *k;
		k = getenv("OPENAI_API_KEY");
		if(k != NULL && *k != '\0'){
			Provider *p = provnew(Popenai, "openai", k, "gpt-4o");
			cfg->provs = erealloc(cfg->provs,
				sizeof(Provider*) * (cfg->nprov + 1));
			cfg->provs[cfg->nprov++] = p;
		}
		k = getenv("ANTHROPIC_API_KEY");
		if(k != NULL && *k != '\0'){
			Provider *p = provnew(Pclaude, "claude", k, "claude-sonnet-4-20250514");
			cfg->provs = erealloc(cfg->provs,
				sizeof(Provider*) * (cfg->nprov + 1));
			cfg->provs[cfg->nprov++] = p;
		}
		k = getenv("AIRC_LOCAL_URL");
		if(k != NULL && *k != '\0'){
			Provider *p = provnew(Plocal, "local", k, "llama3");
			cfg->provs = erealloc(cfg->provs,
				sizeof(Provider*) * (cfg->nprov + 1));
			cfg->provs[cfg->nprov++] = p;
		}
	}

	return cfg;
}

/*
 * Resolve a model spec like "openai:gpt-4o" or "claude"
 * to a provider, optionally overriding the model.
 */
Provider*
configprov(Config *cfg, char *spec)
{
	char *colon, *pname, *mname;
	int i;

	if(spec == NULL)
		spec = cfg->model;

	pname = estrdup(spec);
	colon = strchr(pname, ':');
	if(colon != NULL){
		*colon = '\0';
		mname = colon + 1;
	}else{
		mname = NULL;
	}

	for(i = 0; i < cfg->nprov; i++){
		if(strcmp(cfg->provs[i]->name, pname) == 0){
			Provider *p = cfg->provs[i];
			if(mname != NULL && *mname != '\0'){
				free(p->model);
				p->model = estrdup(mname);
			}
			free(pname);
			return p;
		}
	}

	/* try type prefix match */
	for(i = 0; i < cfg->nprov; i++){
		Provider *p = cfg->provs[i];
		if((strcmp(pname, "openai") == 0 && p->type == Popenai)
		|| (strcmp(pname, "claude") == 0 && p->type == Pclaude)
		|| (strcmp(pname, "local") == 0 && p->type == Plocal)){
			if(mname != NULL && *mname != '\0'){
				free(p->model);
				p->model = estrdup(mname);
			}
			free(pname);
			return p;
		}
	}

	free(pname);
	return cfg->nprov > 0 ? cfg->provs[0] : NULL;
}

void
configfree(Config *cfg)
{
	int i;

	if(cfg == NULL)
		return;
	free(cfg->dir);
	free(cfg->model);
	for(i = 0; i < cfg->nprov; i++)
		provfree(cfg->provs[i]);
	free(cfg->provs);
	free(cfg);
}
