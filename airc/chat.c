/*
 * chat.c - conversation and message management
 *
 * Manages linked lists of chat messages and
 * serializes conversations to JSON for API requests.
 */

#include "airc.h"

Msg*
msgnew(char *role, char *content)
{
	Msg *m;

	m = emalloc(sizeof *m);
	m->role = estrdup(role);
	m->content = estrdup(content);
	m->next = NULL;
	return m;
}

void
msgfree(Msg *m)
{
	if(m == NULL)
		return;
	free(m->role);
	free(m->content);
	free(m);
}

Conv*
convnew(void)
{
	Conv *c;

	c = emalloc(sizeof *c);
	c->head = NULL;
	c->tail = NULL;
	c->n = 0;
	return c;
}

void
convfree(Conv *c)
{
	Msg *m, *next;

	if(c == NULL)
		return;
	for(m = c->head; m != NULL; m = next){
		next = m->next;
		msgfree(m);
	}
	free(c);
}

void
convadd(Conv *c, char *role, char *content)
{
	Msg *m;

	m = msgnew(role, content);
	if(c->tail == NULL){
		c->head = m;
		c->tail = m;
	}else{
		c->tail->next = m;
		c->tail = m;
	}
	c->n++;
}

/*
 * Build JSON messages array for the conversation.
 * For Claude: system message is separate, not in messages array.
 * For OpenAI: system message is part of messages array.
 */
char*
convjson(Conv *c, Provider *p)
{
	Buf b;
	Msg *m;
	int first;

	bufinit(&b);

	if(p->type == Pclaude){
		/*
		 * Anthropic format:
		 * { "model": "...", "max_tokens": N,
		 *   "system": "...",
		 *   "messages": [...] }
		 */
		bufaddstr(&b, "{\"model\":");
		jsonesc(&b, p->model);
		bufaddfmt(&b, ",\"max_tokens\":%d", p->maxtoken);

		/* extract system message */
		for(m = c->head; m != NULL; m = m->next){
			if(strcmp(m->role, "system") == 0){
				bufaddstr(&b, ",\"system\":");
				jsonesc(&b, m->content);
				break;
			}
		}

		bufaddstr(&b, ",\"messages\":[");
		first = 1;
		for(m = c->head; m != NULL; m = m->next){
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
		bufaddstr(&b, "],\"stream\":true}");
	}else{
		/*
		 * OpenAI format:
		 * { "model": "...", "messages": [...],
		 *   "stream": true }
		 */
		bufaddstr(&b, "{\"model\":");
		jsonesc(&b, p->model);
		bufaddstr(&b, ",\"messages\":[");
		first = 1;
		for(m = c->head; m != NULL; m = m->next){
			if(!first)
				bufaddc(&b, ',');
			bufaddstr(&b, "{\"role\":");
			jsonesc(&b, m->role);
			bufaddstr(&b, ",\"content\":");
			jsonesc(&b, m->content);
			bufaddc(&b, '}');
			first = 0;
		}
		bufaddstr(&b, "],\"stream\":true}");
	}

	return bufstr(&b);
}
