/*
 * utils.h - Small generic helpers used by both server and client.
 */
#ifndef NCS_UTILS_H
#define NCS_UTILS_H

#include <sys/types.h>
#include <stddef.h>

/* Tokenize `line` in-place. Quoted segments (' or ") stay together.
 * Fills argv[] and returns argc. argv[argc] is set to NULL.            */
int      parse_args(char *line, char **argv, int max_args);

/* Strip leading/trailing whitespace and CR/LF in-place. */
void     trim(char *s);

/* send(2) with EINTR + short-write loop. Returns total bytes or -1. */
ssize_t  send_all(int fd, const char *buf, size_t len);

/* Set the given fd to non-blocking. */
int      set_nonblocking(int fd);

/* Thread-safe timestamped logger. */
void     log_msg(const char *level, const char *fmt, ...);

#endif /* NCS_UTILS_H */
