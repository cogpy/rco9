# airc - Plan 9 style AI chat for rc shell
#
# Build:   make
# Install: make install
# Clean:   make clean

PREFIX = /usr/local
CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -O2
LDFLAGS =

SRCS = main.c buf.c util.c json.c http.c config.c \
       chat.c api.c role.c session.c shell.c repl.c
OBJS = $(SRCS:.c=.o)
BIN = airc

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c $<

# dependencies (all include airc.h)
main.o: airc.h
buf.o: airc.h
util.o: airc.h
json.o: airc.h
http.o: airc.h
config.o: airc.h
chat.o: airc.h
api.o: airc.h
role.o: airc.h
session.o: airc.h
shell.o: airc.h
repl.o: airc.h

install: $(BIN)
	mkdir -p $(PREFIX)/bin
	cp $(BIN) $(PREFIX)/bin/
	chmod 755 $(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(PREFIX)/bin/$(BIN)

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all install uninstall clean
