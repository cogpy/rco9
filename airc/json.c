/*
 * json.c - minimal JSON parser and builder
 *
 * Recursive descent parser for JSON values.
 * Builder helpers for constructing API request bodies.
 * No external dependencies.
 */

#include "airc.h"

static Json *parsevalue(char**);
static Json *parsestring(char**);
static Json *parsenumber(char**);
static Json *parseobject(char**);
static Json *parsearray(char**);
static Json *parsebool(char**);
static Json *parsenull(char**);

static char*
skipws(char *s)
{
	while(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
		s++;
	return s;
}

static char*
parseesc(char **sp, Buf *b)
{
	char *s;
	int i;
	unsigned u;
	char utf[8];

	s = *sp;
	if(*s != '"')
		return NULL;
	s++;
	while(*s != '\0' && *s != '"'){
		if(*s == '\\'){
			s++;
			switch(*s){
			case '"':  bufaddc(b, '"'); break;
			case '\\': bufaddc(b, '\\'); break;
			case '/':  bufaddc(b, '/'); break;
			case 'b':  bufaddc(b, '\b'); break;
			case 'f':  bufaddc(b, '\f'); break;
			case 'n':  bufaddc(b, '\n'); break;
			case 'r':  bufaddc(b, '\r'); break;
			case 't':  bufaddc(b, '\t'); break;
			case 'u':
				s++;
				u = 0;
				for(i = 0; i < 4 && *s; i++, s++){
					u <<= 4;
					if(*s >= '0' && *s <= '9')
						u |= *s - '0';
					else if(*s >= 'a' && *s <= 'f')
						u |= *s - 'a' + 10;
					else if(*s >= 'A' && *s <= 'F')
						u |= *s - 'A' + 10;
				}
				/* simple UTF-8 encode */
				if(u < 0x80){
					utf[0] = u;
					bufadd(b, utf, 1);
				}else if(u < 0x800){
					utf[0] = 0xC0 | (u >> 6);
					utf[1] = 0x80 | (u & 0x3F);
					bufadd(b, utf, 2);
				}else{
					utf[0] = 0xE0 | (u >> 12);
					utf[1] = 0x80 | ((u >> 6) & 0x3F);
					utf[2] = 0x80 | (u & 0x3F);
					bufadd(b, utf, 3);
				}
				continue; /* already advanced s */
			default:
				bufaddc(b, *s);
				break;
			}
			s++;
		}else{
			bufaddc(b, *s);
			s++;
		}
	}
	if(*s == '"')
		s++;
	*sp = s;
	return bufstr(b);
}

static Json*
parsestring(char **sp)
{
	Json *j;
	Buf b;

	bufinit(&b);
	j = emalloc(sizeof *j);
	j->type = Jstring;
	parseesc(sp, &b);
	j->str = estrdup(bufstr(&b));
	buffree(&b);
	return j;
}

static Json*
parsenumber(char **sp)
{
	Json *j;
	char *s, *end;

	s = *sp;
	j = emalloc(sizeof *j);
	j->type = Jnumber;
	j->num = strtod(s, &end);
	*sp = end;
	return j;
}

static Json*
parseobject(char **sp)
{
	Json *j, *field, *tail;
	char *s;
	Buf kb;

	s = *sp;
	if(*s != '{')
		return NULL;
	s++;

	j = emalloc(sizeof *j);
	j->type = Jobject;
	tail = NULL;

	s = skipws(s);
	while(*s != '\0' && *s != '}'){
		if(*s != '"'){
			s = skipws(++s);
			continue;
		}
		/* parse key */
		bufinit(&kb);
		parseesc(&s, &kb);

		s = skipws(s);
		if(*s == ':')
			s++;
		s = skipws(s);

		/* parse value */
		field = parsevalue(&s);
		if(field != NULL){
			field->key = estrdup(bufstr(&kb));
			if(tail == NULL)
				j->child = field;
			else
				tail->next = field;
			tail = field;
		}
		buffree(&kb);

		s = skipws(s);
		if(*s == ',')
			s++;
		s = skipws(s);
	}
	if(*s == '}')
		s++;
	*sp = s;
	return j;
}

static Json*
parsearray(char **sp)
{
	Json *j, *elem, *tail;
	char *s;

	s = *sp;
	if(*s != '[')
		return NULL;
	s++;

	j = emalloc(sizeof *j);
	j->type = Jarray;
	tail = NULL;

	s = skipws(s);
	while(*s != '\0' && *s != ']'){
		elem = parsevalue(&s);
		if(elem != NULL){
			if(tail == NULL)
				j->child = elem;
			else
				tail->next = elem;
			tail = elem;
		}
		s = skipws(s);
		if(*s == ',')
			s++;
		s = skipws(s);
	}
	if(*s == ']')
		s++;
	*sp = s;
	return j;
}

static Json*
parsebool(char **sp)
{
	Json *j;
	char *s;

	s = *sp;
	j = emalloc(sizeof *j);
	j->type = Jbool;
	if(strncmp(s, "true", 4) == 0){
		j->bval = 1;
		*sp = s + 4;
	}else{
		j->bval = 0;
		*sp = s + 5;
	}
	return j;
}

static Json*
parsenull(char **sp)
{
	Json *j;

	j = emalloc(sizeof *j);
	j->type = Jnull;
	*sp += 4;
	return j;
}

static Json*
parsevalue(char **sp)
{
	char *s;

	s = skipws(*sp);
	*sp = s;

	switch(*s){
	case '"':
		return parsestring(sp);
	case '{':
		return parseobject(sp);
	case '[':
		return parsearray(sp);
	case 't':
	case 'f':
		return parsebool(sp);
	case 'n':
		return parsenull(sp);
	default:
		if(*s == '-' || (*s >= '0' && *s <= '9'))
			return parsenumber(sp);
		return NULL;
	}
}

Json*
jsonparse(char *s)
{
	if(s == NULL)
		return NULL;
	s = skipws(s);
	return parsevalue(&s);
}

void
jsonfree(Json *j)
{
	Json *next;

	while(j != NULL){
		next = j->next;
		free(j->key);
		free(j->str);
		jsonfree(j->child);
		free(j);
		j = next;
	}
}

Json*
jsonget(Json *j, char *key)
{
	Json *c;

	if(j == NULL || j->type != Jobject)
		return NULL;
	for(c = j->child; c != NULL; c = c->next)
		if(c->key != NULL && strcmp(c->key, key) == 0)
			return c;
	return NULL;
}

Json*
jsonidx(Json *j, int i)
{
	Json *c;
	int n;

	if(j == NULL || j->type != Jarray)
		return NULL;
	n = 0;
	for(c = j->child; c != NULL; c = c->next){
		if(n == i)
			return c;
		n++;
	}
	return NULL;
}

char*
jsonstr(Json *j)
{
	if(j == NULL)
		return NULL;
	if(j->type == Jstring)
		return j->str;
	return NULL;
}

double
jsonnum(Json *j)
{
	if(j != NULL && j->type == Jnumber)
		return j->num;
	return 0;
}

int
jsonbval(Json *j)
{
	if(j != NULL && j->type == Jbool)
		return j->bval;
	return 0;
}

int
jsonlen(Json *j)
{
	Json *c;
	int n;

	if(j == NULL)
		return 0;
	if(j->type != Jarray && j->type != Jobject)
		return 0;
	n = 0;
	for(c = j->child; c != NULL; c = c->next)
		n++;
	return n;
}

/*
 * Escape a string for JSON output.
 * Writes the quoted, escaped string into buf.
 */
void
jsonesc(Buf *b, char *s)
{
	bufaddc(b, '"');
	if(s == NULL){
		bufaddc(b, '"');
		return;
	}
	for(; *s; s++){
		switch(*s){
		case '"':  bufaddstr(b, "\\\""); break;
		case '\\': bufaddstr(b, "\\\\"); break;
		case '\b': bufaddstr(b, "\\b"); break;
		case '\f': bufaddstr(b, "\\f"); break;
		case '\n': bufaddstr(b, "\\n"); break;
		case '\r': bufaddstr(b, "\\r"); break;
		case '\t': bufaddstr(b, "\\t"); break;
		default:
			if((uchar)*s < 0x20)
				bufaddfmt(b, "\\u%04x", (uchar)*s);
			else
				bufaddc(b, *s);
			break;
		}
	}
	bufaddc(b, '"');
}
