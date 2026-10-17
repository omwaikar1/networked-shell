/*
 * utils.c - Generic helpers used by both the server and the client.
 *
 * Author : Samruddhi Navale
 * Role   : Tokenizer with quote handling, in-place trim, robust
 *          send_all (handles short writes and EINTR), a non-blocking
 *          fd helper, and a small mutex-protected logger.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/socket.h>

#include "utils.h"

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* parse_args - in-place tokenizer that respects single + double       */
/* quotes.  Modifies `line` by inserting NULs and returns argc.        */
/* ------------------------------------------------------------------ */
int parse_args(char *line, char **argv, int max_args)
{
    int   argc = 0;
    char *p    = line;

    while (*p && argc < max_args - 1) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (*p == '"' || *p == '\'') {
            char q = *p++;
            argv[argc++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* ------------------------------------------------------------------ */
/* trim - strip leading + trailing whitespace and CR/LF in-place.      */
/* ------------------------------------------------------------------ */
void trim(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if (c == '\n' || c == '\r' || isspace(c)) {
            s[--n] = '\0';
        } else {
            break;
        }
    }
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

/* ------------------------------------------------------------------ */
/* send_all - loop around send() handling short writes + EINTR.         */
/* ------------------------------------------------------------------ */
ssize_t send_all(int fd, const char *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

/* ------------------------------------------------------------------ */
/* set_nonblocking                                                     */
/* ------------------------------------------------------------------ */
int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ------------------------------------------------------------------ */
/* log_msg - thread-safe timestamped logger, writes to stderr.          */
/* ------------------------------------------------------------------ */
void log_msg(const char *level, const char *fmt, ...)
{
    pthread_mutex_lock(&log_lock);

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(stderr, "[%s] [%s] ", ts, level);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
    fflush(stderr);

    pthread_mutex_unlock(&log_lock);
}
