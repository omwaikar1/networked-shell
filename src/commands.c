/*
 * commands.c - Built-in command table and external-command runner.
 *
 * Author : Atharva Tupe
 * Role   : Implements `help`, `cd`, `pwd`, `whoami`, `history`,
 *          `sessions`, `echo`, `uptime`, `clear`, `exit/quit`, and
 *          wraps anything else in `/bin/sh -c` so the user gets a
 *          real shell over the wire.  Output is streamed back to
 *          the client via a pipe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#include "commands.h"
#include "messaging.h"
#include "session.h"
#include "utils.h"

static int builtin_help    (Session *s, int argc, char **argv);
static int builtin_exit    (Session *s, int argc, char **argv);
static int builtin_cd      (Session *s, int argc, char **argv);
static int builtin_pwd     (Session *s, int argc, char **argv);
static int builtin_whoami  (Session *s, int argc, char **argv);
static int builtin_history (Session *s, int argc, char **argv);
static int builtin_sessions(Session *s, int argc, char **argv);
static int builtin_echo    (Session *s, int argc, char **argv);
static int builtin_uptime  (Session *s, int argc, char **argv);
static int builtin_clear   (Session *s, int argc, char **argv);

static Builtin builtins[] = {
    {"help",     builtin_help,     "List available built-in commands"},
    {"exit",     builtin_exit,     "Disconnect from the server"},
    {"quit",     builtin_exit,     "Alias for exit"},
    {"cd",       builtin_cd,       "Change session working directory: cd <path>"},
    {"pwd",      builtin_pwd,      "Print this session's current directory"},
    {"whoami",   builtin_whoami,   "Print the user the server runs as"},
    {"history",  builtin_history,  "Show command history for this session"},
    {"sessions", builtin_sessions, "List active client sessions"},
    {"echo",     builtin_echo,     "Echo arguments back to the client"},
    {"uptime",   builtin_uptime,   "How long this session has been connected"},
    {"clear",    builtin_clear,    "Clear the client terminal"},
    {"nick",     builtin_nick,     "Show or set this session's nickname"},
    {"who",      builtin_who,      "List connected users with nicknames"},
    {"msg",      builtin_msg,      "Send a message: msg <session_id> <text>"},
    {"broadcast",builtin_broadcast,"Send a message to every other session"},
    {NULL,       NULL,             NULL},
};

int execute_command(Session *s, const char *line)
{
    char  buf[CMD_SIZE];
    char *argv[MAX_ARGS];

    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int argc = parse_args(buf, argv, MAX_ARGS);
    if (argc == 0) return 0;

    int rc = run_builtin(s, argc, argv);
    if (rc == -2) {
        return run_external(s, line);
    }
    return rc;
}

int run_builtin(Session *s, int argc, char **argv)
{
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(builtins[i].name, argv[0]) == 0) {
            return builtins[i].fn(s, argc, argv);
        }
    }
    return -2;
}

int run_external(Session *s, const char *line)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        session_send(s, "[ncs] error: pipe() failed\n");
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        session_send(s, "[ncs] error: fork() failed\n");
        close(pipefd[0]); close(pipefd[1]);
        return 0;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (chdir(s->cwd) != 0) {
            fprintf(stderr, "[ncs] cannot chdir to %s\n", s->cwd);
            _exit(127);
        }
        execl("/bin/sh", "sh", "-c", line, (char *)NULL);
        fprintf(stderr, "[ncs] exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    char    rbuf[BUF_SIZE];
    ssize_t n;
    while ((n = read(pipefd[0], rbuf, sizeof(rbuf))) > 0) {
        if (send_all(s->sockfd, rbuf, (size_t)n) < 0) break;
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    char tail[64];
    int  code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    snprintf(tail, sizeof(tail), "[exit %d]\n", code);
    session_send(s, tail);
    return 0;
}

/* ================================================================ */
/*  Built-in implementations                                          */
/* ================================================================ */
static int builtin_help(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    session_send(s, "Built-in commands:\n");
    for (int i = 0; builtins[i].name; i++) {
        char line[256];
        snprintf(line, sizeof(line), "  %-10s - %s\n",
                 builtins[i].name, builtins[i].help);
        session_send(s, line);
    }
    session_send(s,
        "Anything else is executed via `/bin/sh -c` inside your "
        "isolated session.\n");
    return 0;
}

static int builtin_exit(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    session_send(s, "Goodbye!\n");
    s->active = 0;
    return -1;
}

static int builtin_cd(Session *s, int argc, char **argv)
{
    const char *target = (argc < 2) ? getenv("HOME") : argv[1];
    if (!target || !*target) target = "/";

    char abs[PATH_MAX_LEN * 2];
    if (target[0] == '/') {
        snprintf(abs, sizeof(abs), "%s", target);
    } else {
        snprintf(abs, sizeof(abs), "%s/%s", s->cwd, target);
    }

    /* Pass NULL so realpath allocates a PATH_MAX-sized buffer; safer
     * than a fixed stack buffer that might be smaller than PATH_MAX. */
    char *resolved = realpath(abs, NULL);
    if (resolved == NULL) {
        char msg[PATH_MAX_LEN + 64];
        snprintf(msg, sizeof(msg), "cd: %s: %s\n", target, strerror(errno));
        session_send(s, msg);
        return 0;
    }

    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) {
        session_send(s, "cd: not a directory\n");
        free(resolved);
        return 0;
    }
    if (access(resolved, X_OK) != 0) {
        session_send(s, "cd: permission denied\n");
        free(resolved);
        return 0;
    }
    if (strlen(resolved) >= sizeof(s->cwd)) {
        session_send(s, "cd: path too long\n");
        free(resolved);
        return 0;
    }

    strncpy(s->cwd, resolved, sizeof(s->cwd) - 1);
    s->cwd[sizeof(s->cwd) - 1] = '\0';
    free(resolved);
    return 0;
}

static int builtin_pwd(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    char line[PATH_MAX_LEN + 2];
    snprintf(line, sizeof(line), "%s\n", s->cwd);
    session_send(s, line);
    return 0;
}

static int builtin_whoami(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    struct passwd *pw = getpwuid(geteuid());
    char line[128];
    snprintf(line, sizeof(line), "%s\n", pw ? pw->pw_name : "unknown");
    session_send(s, line);
    return 0;
}

static int builtin_history(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    if (s->history_count == 0) {
        session_send(s, "(no history yet)\n");
        return 0;
    }
    for (int i = 0; i < s->history_count; i++) {
        char line[CMD_SIZE + 16];
        snprintf(line, sizeof(line), "%4d  %s\n", i + 1, s->history[i]);
        session_send(s, line);
    }
    return 0;
}

static int builtin_sessions(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;

    pthread_mutex_lock(&g_sessions_lock);
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_sessions[i]) {
            char    line[PATH_MAX_LEN + 128];
            time_t  up = time(NULL) - g_sessions[i]->connected_at;
            snprintf(line, sizeof(line),
                "  [%2d] ip=%s up=%lds cwd=%s%s\n",
                g_sessions[i]->id,
                inet_ntoa(g_sessions[i]->addr.sin_addr),
                (long)up,
                g_sessions[i]->cwd,
                g_sessions[i] == s ? "  (you)" : "");
            session_send(s, line);
            count++;
        }
    }
    pthread_mutex_unlock(&g_sessions_lock);

    char tail[64];
    snprintf(tail, sizeof(tail), "Active sessions: %d / %d\n",
             count, MAX_CLIENTS);
    session_send(s, tail);
    return 0;
}

static int builtin_echo(Session *s, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        session_send(s, argv[i]);
        if (i < argc - 1) session_send(s, " ");
    }
    session_send(s, "\n");
    return 0;
}

static int builtin_uptime(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    time_t up = time(NULL) - s->connected_at;
    char   line[128];
    snprintf(line, sizeof(line),
        "Session %d uptime: %ld seconds\n", s->id, (long)up);
    session_send(s, line);
    return 0;
}

static int builtin_clear(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    session_send(s, "\033[2J\033[H");
    return 0;
}
