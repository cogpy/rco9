/*
 * role.c - role management
 *
 * Roles are system prompts stored as simple text files
 * in ~/.airc/roles/. Each file has:
 *   prompt: <text>
 *   model: <optional model override>
 *
 * Built-in roles:
 *   %shell% - rc shell command assistant
 *   %code%  - code generation assistant
 */

#include "airc.h"

static char *shellrole =
	"You are an rc shell command assistant. The user describes what they "
	"want to do in natural language and you respond with ONLY the rc shell "
	"command(s) to accomplish it. Do not include explanations, markdown "
	"formatting, or code fences. Output only the raw command(s). "
	"Use rc shell syntax (not bash/sh). Key rc differences: "
	"lists use parentheses (a b c), variables are $var, "
	"if/else uses braces {}, for loops: for(i in list) cmd, "
	"command substitution uses backticks `cmd`, "
	"no [[ ]] test syntax - use test(1) or ~ for pattern matching, "
	"functions: fn name { body }, "
	"heredocs: cmd << 'EOF'\\ntext\\nEOF";

static char *coderole =
	"You are a code assistant. Respond with clean, well-structured code. "
	"Use minimal comments. Do not include explanations outside the code "
	"unless specifically asked. Output code without markdown fences.";

/*
 * Load a role by name.
 * First checks built-in roles (%name%),
 * then looks in ~/.airc/roles/<name>.
 */
Role*
roleload(Config *cfg, char *name)
{
	Role *r;
	char *path, *data, *line, *next;

	r = emalloc(sizeof *r);
	r->name = estrdup(name);
	r->prompt = NULL;
	r->model = NULL;

	/* built-in roles */
	if(strcmp(name, "shell") == 0 || strcmp(name, "%shell%") == 0){
		r->prompt = estrdup(shellrole);
		return r;
	}
	if(strcmp(name, "code") == 0 || strcmp(name, "%code%") == 0){
		r->prompt = estrdup(coderole);
		return r;
	}

	/* load from file */
	path = smprint("%s/roles/%s", cfg->dir, name);
	data = readfile(path);
	free(path);

	if(data == NULL){
		warn("role '%s' not found", name);
		free(r->name);
		free(r);
		return NULL;
	}

	for(line = data; line != NULL && *line != '\0'; line = next){
		next = strchr(line, '\n');
		if(next != NULL)
			*next++ = '\0';

		line = trim(line);
		if(*line == '\0' || *line == '#')
			continue;

		if(strncmp(line, "prompt:", 7) == 0){
			r->prompt = estrdup(trim(line + 7));
		}else if(strncmp(line, "model:", 6) == 0){
			r->model = estrdup(trim(line + 6));
		}
	}
	free(data);

	if(r->prompt == NULL){
		/* treat entire file as the prompt */
		data = readfile(smprint("%s/roles/%s", cfg->dir, name));
		if(data != NULL)
			r->prompt = data;
	}

	return r;
}

void
rolefree(Role *r)
{
	if(r == NULL)
		return;
	free(r->name);
	free(r->prompt);
	free(r->model);
	free(r);
}
