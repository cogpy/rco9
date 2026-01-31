AIRC - Plan 9 Style AI Chat for rc Shell

airc is an LLM CLI tool written in pure C, designed in the Plan 9
tradition for the rc shell. It provides conversational AI access
from the command line with native rc shell integration.

Inspired by sigoden/aichat, rebuilt from scratch in C with Plan 9
sensibilities: small, composable, no unnecessary dependencies.

FEATURES

  - Single-shot queries from the command line
  - Interactive REPL with dot-commands and sessions
  - rc shell assistant mode (-e): natural language to rc commands
  - Code generation mode (-c): raw code output
  - Pipe/stdin support for composing with Unix tools
  - OpenAI, Anthropic Claude, and local (Ollama) providers
  - Streaming output (SSE)
  - Roles (system prompts) with built-in shell and code roles
  - Session persistence for multi-turn conversations
  - File inclusion for context
  - No external C library dependencies (uses curl subprocess for HTTP)

BUILD

  make

INSTALL

  make install              # to /usr/local/bin
  make PREFIX=/usr install  # custom prefix

QUICK START

  # Set an API key
  export OPENAI_API_KEY=sk-...
  # or
  export ANTHROPIC_API_KEY=sk-ant-...

  # Single query
  airc "explain rc shell lists"

  # Shell assistant - generates rc commands
  airc -e "find all .c files larger than 10k"

  # Code generation
  airc -c "write a quicksort in C"

  # Pipe input
  cat main.c | airc "explain this code"

  # Interactive REPL
  airc

  # Use specific model
  airc -m claude:claude-sonnet-4-20250514 "hello"

  # With role
  airc -r shell "list running processes"

  # With file context
  airc -f bug.log "what went wrong?"

CONFIGURATION

  ~/.airc/config       main configuration (key value format)
  ~/.airc/keys         API provider keys
  ~/.airc/roles/       custom role definitions
  ~/.airc/sessions/    saved conversation sessions

See config.def and keys.example for format reference.

REPL COMMANDS

  .help              show help
  .model [spec]      show/switch model
  .role [name]       activate/deactivate role
  .session [name]    start/switch session
  .save              save current session
  .clear             clear conversation
  .shell <text>      generate rc command
  .file <path>       include file in next message
  .info              show configuration
  .exit              quit

ARCHITECTURE

  airc.h      master header - all types, constants, prototypes
  main.c      entry point and argument parsing
  buf.c       dynamic string buffer
  json.c      recursive descent JSON parser and builder
  util.c      memory, string, path utilities
  http.c      HTTP client via curl subprocess
  api.c       LLM provider abstraction (OpenAI, Claude, local)
  config.c    configuration file loading
  chat.c      conversation/message management
  role.c      role (system prompt) management
  session.c   session persistence
  shell.c     rc shell integration and command execution
  repl.c      interactive REPL with dot-commands

PLAN 9 DESIGN NOTES

  - Pure C (C99), no C++ or Rust dependencies
  - Plan 9 naming: lowercase concatenated function names,
    CamelCase types, UPPERCASE constants
  - Composition: HTTP via curl subprocess (Plan 9 would use webfs)
  - Arena-style buffer management
  - K&R formatting with tabs
  - Shell execution via rc (not bash/sh)
  - Minimal abstraction, maximum clarity

LICENSE

  zlib (same as rc shell)
