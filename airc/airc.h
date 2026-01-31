/*
 * airc.h - Plan 9 style AI chat for rc shell
 *
 * A CLI tool for interacting with LLMs, designed
 * in the Plan 9 tradition: small, composable, clean.
 * Integrates with the rc shell for command generation.
 */

#ifndef AIRC_H
#define AIRC_H

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <ctype.h>
#include <time.h>

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned char uchar;

enum {
	Maxline = 8192,
	Maxpath = 1024,
	Maxhdr = 16,
	Maxhist = 256,
	Maxdepth = 32,
};

enum { False, True };

/* JSON value types */
enum {
	Jnull,
	Jbool,
	Jnumber,
	Jstring,
	Jarray,
	Jobject,
};

/* API provider types */
enum {
	Popenai,
	Pclaude,
	Plocal,
};

/* Operating modes */
enum {
	Mcmd,		/* single-shot command */
	Mrepl,		/* interactive REPL */
	Mshell,		/* rc shell assistant */
	Mcode,		/* code-only output */
};

/* Dynamic string buffer */
typedef struct Buf Buf;
struct Buf {
	char	*s;
	int	len;
	int	cap;
};

/* JSON value */
typedef struct Json Json;
struct Json {
	int	type;
	char	*key;		/* field name if inside object */
	char	*str;
	double	num;
	int	bval;
	Json	*child;		/* first child for object/array */
	Json	*next;		/* next sibling */
};

/* Chat message */
typedef struct Msg Msg;
struct Msg {
	char	*role;		/* system, user, assistant */
	char	*content;
	Msg	*next;
};

/* Conversation */
typedef struct Conv Conv;
struct Conv {
	Msg	*head;
	Msg	*tail;
	int	n;
};

/* LLM provider */
typedef struct Provider Provider;
struct Provider {
	int	type;
	char	*name;
	char	*apibase;
	char	*apikey;
	char	*model;
	int	maxtoken;
};

/* Role definition */
typedef struct Role Role;
struct Role {
	char	*name;
	char	*prompt;
	char	*model;
};

/* Session state */
typedef struct Session Session;
struct Session {
	char	*name;
	char	*path;
	Conv	conv;
};

/* Configuration */
typedef struct Config Config;
struct Config {
	char	*dir;
	char	*model;
	int	stream;
	int	temp;		/* temperature * 100 (e.g. 70 = 0.7) */
	int	maxtoken;
	Provider **provs;
	int	nprov;
	Provider *curprov;
};

/* buf.c */
void	bufinit(Buf*);
void	bufgrow(Buf*, int);
void	bufadd(Buf*, char*, int);
void	bufaddstr(Buf*, char*);
void	bufaddc(Buf*, int);
void	bufaddfmt(Buf*, char*, ...);
void	bufreset(Buf*);
void	buffree(Buf*);
char*	bufstr(Buf*);

/* json.c */
Json*	jsonparse(char*);
void	jsonfree(Json*);
Json*	jsonget(Json*, char*);
Json*	jsonidx(Json*, int);
char*	jsonstr(Json*);
double	jsonnum(Json*);
int	jsonbval(Json*);
int	jsonlen(Json*);
void	jsonesc(Buf*, char*);

/* util.c */
void*	emalloc(ulong);
void*	erealloc(void*, ulong);
char*	estrdup(char*);
void	fatal(char*, ...);
void	warn(char*, ...);
char*	smprint(char*, ...);
int	isterm(int);
char*	homedir(void);
char*	pathjoin(char*, char*);
int	mkdirp(char*);
char*	readfile(char*);
char*	trim(char*);

/* http.c */
int	httppost(char*, char**, int, char*, Buf*);
int	httpstream(char*, char**, int, char*, void(*)(char*, int, void*), void*);

/* api.c */
Provider*	provnew(int, char*, char*, char*);
void		provfree(Provider*);
int		aicomplete(Provider*, Conv*, Config*, Buf*);
int		aistream(Provider*, Conv*, Config*, void(*)(char*, int, void*), void*);
char*		provurl(Provider*);

/* config.c */
Config*	configload(char*);
void	configfree(Config*);
char*	configdir(void);
Provider* configprov(Config*, char*);

/* chat.c */
Conv*	convnew(void);
void	convfree(Conv*);
void	convadd(Conv*, char*, char*);
Msg*	msgnew(char*, char*);
void	msgfree(Msg*);
char*	convjson(Conv*, Provider*);

/* role.c */
Role*	roleload(Config*, char*);
void	rolefree(Role*);

/* session.c */
Session*	sessionnew(char*);
Session*	sessionload(Config*, char*);
int		sessionsave(Config*, Session*);
void		sessionfree(Session*);

/* shell.c */
char*	shellprompt(void);
int	shellexec(char*);
int	shellconfirm(char*);
char*	shelldetect(void);

/* repl.c */
void	replrun(Config*, Provider*, Session*, Role*);

#endif
