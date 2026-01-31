/*
 * session.c - conversation session persistence
 *
 * Sessions store conversation history to disk as JSON,
 * allowing conversations to be resumed across invocations.
 * Stored in ~/.airc/sessions/<name>.json
 */

#include "airc.h"

Session*
sessionnew(char *name)
{
	Session *s;

	s = emalloc(sizeof *s);
	if(name != NULL)
		s->name = estrdup(name);
	else
		s->name = smprint("tmp-%ld", (long)time(NULL));
	s->path = NULL;
	s->conv.head = NULL;
	s->conv.tail = NULL;
	s->conv.n = 0;
	return s;
}

/*
 * Load a session from disk.
 * Returns NULL if session doesn't exist.
 */
Session*
sessionload(Config *cfg, char *name)
{
	Session *s;
	char *path, *data;
	Json *j, *msgs, *m;
	int i, n;

	path = smprint("%s/sessions/%s.json", cfg->dir, name);
	data = readfile(path);
	if(data == NULL){
		free(path);
		return NULL;
	}

	j = jsonparse(data);
	free(data);
	if(j == NULL){
		free(path);
		return NULL;
	}

	s = sessionnew(name);
	s->path = path;

	msgs = jsonget(j, "messages");
	if(msgs != NULL){
		n = jsonlen(msgs);
		for(i = 0; i < n; i++){
			m = jsonidx(msgs, i);
			if(m != NULL){
				Json *role = jsonget(m, "role");
				Json *content = jsonget(m, "content");
				if(role != NULL && content != NULL
				&& jsonstr(role) != NULL && jsonstr(content) != NULL)
					convadd(&s->conv, jsonstr(role), jsonstr(content));
			}
		}
	}

	jsonfree(j);
	return s;
}

/*
 * Save session to disk as JSON.
 */
int
sessionsave(Config *cfg, Session *s)
{
	char *dir, *path;
	FILE *f;
	Msg *m;
	int first;

	dir = smprint("%s/sessions", cfg->dir);
	mkdirp(dir);

	if(s->path != NULL)
		path = s->path;
	else
		path = smprint("%s/%s.json", dir, s->name);
	free(dir);

	f = fopen(path, "w");
	if(f == NULL){
		warn("cannot save session: %s", strerror(errno));
		if(path != s->path)
			free(path);
		return -1;
	}

	fprintf(f, "{\"name\":");
	{
		Buf b;
		bufinit(&b);
		jsonesc(&b, s->name);
		fprintf(f, "%s", bufstr(&b));
		buffree(&b);
	}
	fprintf(f, ",\"messages\":[");

	first = 1;
	for(m = s->conv.head; m != NULL; m = m->next){
		Buf b;
		bufinit(&b);
		if(!first)
			fprintf(f, ",");
		fprintf(f, "{\"role\":");
		jsonesc(&b, m->role);
		fprintf(f, "%s", bufstr(&b));
		bufreset(&b);
		fprintf(f, ",\"content\":");
		jsonesc(&b, m->content);
		fprintf(f, "%s}", bufstr(&b));
		buffree(&b);
		first = 0;
	}
	fprintf(f, "]}\n");
	fclose(f);

	if(s->path == NULL)
		s->path = path;
	else if(path != s->path)
		free(path);
	return 0;
}

void
sessionfree(Session *s)
{
	Msg *m, *next;

	if(s == NULL)
		return;
	free(s->name);
	free(s->path);
	for(m = s->conv.head; m != NULL; m = next){
		next = m->next;
		msgfree(m);
	}
	free(s);
}

char*
sessiondir(Config *cfg)
{
	return smprint("%s/sessions", cfg->dir);
}
