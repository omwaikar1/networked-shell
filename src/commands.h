/*
 * commands.h - Built-in command table and the command dispatcher.
 */
#ifndef NCS_COMMANDS_H
#define NCS_COMMANDS_H

#include "session.h"

typedef int (*BuiltinFn)(Session *s, int argc, char **argv);

typedef struct {
    const char *name;
    BuiltinFn   fn;
    const char *help;
} Builtin;

/* Parse `line` and run a builtin or fork/exec an external command. */
int execute_command(Session *s, const char *line);

/* Returns 0 on success, -1 to terminate the session, -2 if not a builtin. */
int run_builtin(Session *s, int argc, char **argv);

/* Forks /bin/sh -c and streams stdout/stderr back to the client. */
int run_external(Session *s, const char *line);

#endif /* NCS_COMMANDS_H */
