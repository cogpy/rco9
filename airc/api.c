/*
 * api.c - LLM provider abstraction
 *
 * Supports OpenAI, Anthropic Claude, and local
 * (Ollama-compatible) providers. Each provider
 * builds its own request format and parses its
 * own streaming response format.
 */

#include "airc.h"

/* Provider API base URLs */
static char *openaibase = "https://api.openai.com/v1/chat/completions";
static char *claudebase = "https://api.anthropic.com/v1/messages";
static char *localbase  = "http://localhost:11434/v1/chat/completions";

Provider*
provnew(int type, char *name, char *key, char *model)
{
	Provider *p;

	p = emalloc(sizeof *p);
	p->type = type;
	p->name = estrdup(name);
	p->apikey = estrdup(key);
	p->model = estrdup(model);
	p->maxtoken = 4096;

	switch(type){
	case Popenai:
		p->apibase = estrdup(openaibase);
		break;
	case Pclaude:
		p->apibase = estrdup(claudebase);
		break;
	case Plocal:
		/* key is actually the base URL for local */
		free(p->apibase);
		if(key != NULL && strncmp(key, "http", 4) == 0){
			p->apibase = estrdup(key);
			free(p->apikey);
			p->apikey = estrdup("none");
		}else{
			p->apibase = estrdup(localbase);
		}
		break;
	}
	return p;
}

void
provfree(Provider *p)
{
	if(p == NULL)
		return;
	free(p->name);
	free(p->apibase);
	free(p->apikey);
	free(p->model);
	free(p);
}

char*
provurl(Provider *p)
{
	return p->apibase;
}

/*
 * Build HTTP headers for the provider.
 * Returns the number of headers written to hdrs[].
 */
static int
buildhdrs(Provider *p, char **hdrs, int max)
{
	int n;

	(void)max;
	n = 0;
	hdrs[n++] = estrdup("Content-Type: application/json");

	switch(p->type){
	case Popenai:
		hdrs[n++] = smprint("Authorization: Bearer %s", p->apikey);
		break;
	case Pclaude:
		hdrs[n++] = smprint("x-api-key: %s", p->apikey);
		hdrs[n++] = estrdup("anthropic-version: 2023-06-01");
		break;
	case Plocal:
		if(p->apikey != NULL && strcmp(p->apikey, "none") != 0)
			hdrs[n++] = smprint("Authorization: Bearer %s", p->apikey);
		break;
	}
	return n;
}

static void
freehdrs(char **hdrs, int n)
{
	int i;
	for(i = 0; i < n; i++)
		free(hdrs[i]);
}

/*
 * Build the JSON request body.
 * Caller must free the returned string.
 */
static char*
buildreq(Provider *p, Conv *conv, Config *cfg)
{
	Buf b;
	Msg *m;
	int first;

	bufinit(&b);

	if(p->type == Pclaude){
		bufaddstr(&b, "{\"model\":");
		jsonesc(&b, p->model);
		bufaddfmt(&b, ",\"max_tokens\":%d", cfg->maxtoken);

		if(cfg->temp >= 0)
			bufaddfmt(&b, ",\"temperature\":%.2f", cfg->temp / 100.0);

		/* system message separate in Claude API */
		for(m = conv->head; m != NULL; m = m->next){
			if(strcmp(m->role, "system") == 0){
				bufaddstr(&b, ",\"system\":");
				jsonesc(&b, m->content);
				break;
			}
		}

		bufaddstr(&b, ",\"stream\":true");
		bufaddstr(&b, ",\"messages\":[");
		first = 1;
		for(m = conv->head; m != NULL; m = m->next){
			if(strcmp(m->role, "system") == 0)
				continue;
			if(!first)
				bufaddc(&b, ',');
			bufaddstr(&b, "{\"role\":");
			jsonesc(&b, m->role);
			bufaddstr(&b, ",\"content\":");
			jsonesc(&b, m->content);
			bufaddc(&b, '}');
			first = 0;
		}
		bufaddstr(&b, "]}");
	}else{
		/* OpenAI / local format */
		bufaddstr(&b, "{\"model\":");
		jsonesc(&b, p->model);

		if(cfg->temp >= 0)
			bufaddfmt(&b, ",\"temperature\":%.2f", cfg->temp / 100.0);

		bufaddstr(&b, ",\"stream\":true");
		bufaddstr(&b, ",\"messages\":[");
		first = 1;
		for(m = conv->head; m != NULL; m = m->next){
			if(!first)
				bufaddc(&b, ',');
			bufaddstr(&b, "{\"role\":");
			jsonesc(&b, m->role);
			bufaddstr(&b, ",\"content\":");
			jsonesc(&b, m->content);
			bufaddc(&b, '}');
			first = 0;
		}
		bufaddstr(&b, "]}");
	}

	return estrdup(bufstr(&b));
}

/*
 * Parse an OpenAI streaming chunk.
 * Format: {"choices":[{"delta":{"content":"text"}}]}
 * Returns extracted text or NULL.
 */
static char*
parseopenai(char *data)
{
	Json *j, *choices, *first, *delta, *content;
	char *text;

	j = jsonparse(data);
	if(j == NULL)
		return NULL;

	choices = jsonget(j, "choices");
	if(choices == NULL){
		/* check for error */
		Json *err = jsonget(j, "error");
		if(err != NULL){
			Json *msg = jsonget(err, "message");
			if(msg != NULL && jsonstr(msg) != NULL){
				char *e = estrdup(jsonstr(msg));
				jsonfree(j);
				return e;
			}
		}
		jsonfree(j);
		return NULL;
	}

	first = jsonidx(choices, 0);
	if(first == NULL){
		jsonfree(j);
		return NULL;
	}

	delta = jsonget(first, "delta");
	if(delta == NULL){
		jsonfree(j);
		return NULL;
	}

	content = jsonget(delta, "content");
	text = NULL;
	if(content != NULL && jsonstr(content) != NULL)
		text = estrdup(jsonstr(content));

	jsonfree(j);
	return text;
}

/*
 * Parse a Claude streaming chunk.
 * Format: {"type":"content_block_delta","delta":{"type":"text_delta","text":"..."}}
 * Also handles error events.
 */
static char*
parseclaude(char *data)
{
	Json *j, *typ, *delta, *text;
	char *result;

	j = jsonparse(data);
	if(j == NULL)
		return NULL;

	typ = jsonget(j, "type");
	if(typ == NULL || jsonstr(typ) == NULL){
		jsonfree(j);
		return NULL;
	}

	if(strcmp(jsonstr(typ), "content_block_delta") == 0){
		delta = jsonget(j, "delta");
		if(delta != NULL){
			text = jsonget(delta, "text");
			if(text != NULL && jsonstr(text) != NULL){
				result = estrdup(jsonstr(text));
				jsonfree(j);
				return result;
			}
		}
	}else if(strcmp(jsonstr(typ), "error") == 0){
		Json *err = jsonget(j, "error");
		if(err != NULL){
			Json *msg = jsonget(err, "message");
			if(msg != NULL && jsonstr(msg) != NULL){
				result = smprint("[error: %s]", jsonstr(msg));
				jsonfree(j);
				return result;
			}
		}
	}

	jsonfree(j);
	return NULL;
}

/* Stream callback context */
typedef struct Streamctx Streamctx;
struct Streamctx {
	Provider *prov;
	Buf	*accum;		/* accumulated full response */
	void	(*usercb)(char*, int, void*);
	void	*useraux;
};

static void
streamcb(char *data, int len, void *aux)
{
	Streamctx *ctx;
	char *text, *tmp;

	ctx = aux;

	/* null-terminate the data */
	tmp = emalloc(len + 1);
	memcpy(tmp, data, len);
	tmp[len] = '\0';

	switch(ctx->prov->type){
	case Popenai:
	case Plocal:
		text = parseopenai(tmp);
		break;
	case Pclaude:
		text = parseclaude(tmp);
		break;
	default:
		text = NULL;
		break;
	}
	free(tmp);

	if(text != NULL){
		if(ctx->accum != NULL)
			bufaddstr(ctx->accum, text);
		if(ctx->usercb != NULL)
			ctx->usercb(text, strlen(text), ctx->useraux);
		free(text);
	}
}

/*
 * Send a completion request with streaming.
 * Calls cb for each text chunk received.
 * Returns 0 on success, -1 on error.
 */
int
aistream(Provider *p, Conv *conv, Config *cfg,
	void (*cb)(char*, int, void*), void *aux)
{
	char *hdrs[Maxhdr];
	int nhdrs;
	char *body;
	Streamctx ctx;
	Buf accum;
	int ret;

	nhdrs = buildhdrs(p, hdrs, Maxhdr);
	body = buildreq(p, conv, cfg);

	bufinit(&accum);
	ctx.prov = p;
	ctx.accum = &accum;
	ctx.usercb = cb;
	ctx.useraux = aux;

	ret = httpstream(provurl(p), hdrs, nhdrs, body, streamcb, &ctx);

	/* add assistant response to conversation */
	if(accum.len > 0)
		convadd(conv, "assistant", bufstr(&accum));

	buffree(&accum);
	freehdrs(hdrs, nhdrs);
	free(body);
	return ret;
}

/*
 * Send a completion request, collect full response.
 * Response text is written to resp buffer.
 * Returns 0 on success, -1 on error.
 */
int
aicomplete(Provider *p, Conv *conv, Config *cfg, Buf *resp)
{
	char *hdrs[Maxhdr];
	int nhdrs;
	char *body;
	Buf raw;
	int ret;
	Json *j;

	nhdrs = buildhdrs(p, hdrs, Maxhdr);
	body = buildreq(p, conv, cfg);

	/* modify body to disable streaming */
	/* simpler: just use streaming and collect */
	bufinit(&raw);

	Streamctx ctx;
	ctx.prov = p;
	ctx.accum = resp;
	ctx.usercb = NULL;
	ctx.useraux = NULL;

	ret = httpstream(provurl(p), hdrs, nhdrs, body, streamcb, &ctx);

	buffree(&raw);
	freehdrs(hdrs, nhdrs);
	free(body);
	(void)j;
	return ret;
}
