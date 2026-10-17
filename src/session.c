/*
 * session.c - Per-client session state and worker-thread loop.
 *
 * Author : Atharva Tupe
 * Role   : Session lifecycle, prompt loop, isolation of per-client cwd
 *          and history, hand-off to the command dispatcher in commands.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "session.h"
#include "commands.h"
#include "utils.h"

/* ------------------------------------------------------------------ */
/* Construction / destruction                                          */
/* ------------------------------------------------------------------ */
Session *session_create(int sockfd, struct sockaddr_in addr)
{
    Session *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->sockfd        = sockfd;
    s->addr          = addr;
    s->active        = 1;
    s->history_count = 0;
    s->connected_at  = time(NULL);

    if (getcwd(s->cwd, sizeof(s->cwd)) == NULL) {
        strncpy(s->cwd, "/", sizeof(s->cwd) - 1);
    }
    return s;
}

void session_destroy(Session *s)
{
    if (!s) return;
    if (s->sockfd >= 0) close(s->sockfd);
    free(s);
}

/* ------------------------------------------------------------------ */
/* I/O helpers                                                         */
/* ------------------------------------------------------------------ */
int session_send(Session *s, const char *msg)
{
    return (int)send_all(s->sockfd, msg, strlen(msg));
}

int session_recv(Session *s, char *buf, size_t len)
{
    ssize_t n = recv(s->sockfd, buf, len - 1, 0);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* Per-session command history (ring buffer when full)                 */
/* ------------------------------------------------------------------ */
void session_add_history(Session *s, const char *cmd)
{
    if (s->history_count < HISTORY_SIZE) {
        strncpy(s->history[s->history_count], cmd, CMD_SIZE - 1);
        s->history[s->history_count][CMD_SIZE - 1] = '\0';
        s->history_count++;
    } else {
        for (int i = 1; i < HISTORY_SIZE; i++) {
            memcpy(s->history[i - 1], s->history[i], CMD_SIZE);
        }
        strncpy(s->history[HISTORY_SIZE - 1], cmd, CMD_SIZE - 1);
        s->history[HISTORY_SIZE - 1][CMD_SIZE - 1] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Banner + prompt                                                     */
/* ------------------------------------------------------------------ */
static void send_welcome(Session *s)
{
    char banner[640];
    snprintf(banner, sizeof(banner),
        "=========================================================\n"
        " Welcome to the Networked Concurrent Shell (NCS)\n"
        " Session ID : %d\n"
        " Connected  : %s\n"
        " Type 'help' for built-ins, 'exit' to disconnect.\n"
        "=========================================================\n",
        s->id, inet_ntoa(s->addr.sin_addr));
    session_send(s, banner);
}

static void send_prompt(Session *s)
{
    char prompt[PATH_MAX_LEN + 64];
    snprintf(prompt, sizeof(prompt), "ncs[%d]:%s$ ", s->id, s->cwd);
    session_send(s, prompt);
}

/* ------------------------------------------------------------------ */
/* Worker thread entry point                                           */
/* ------------------------------------------------------------------ */
void *session_thread(void *arg)
{
    Session *s = (Session *)arg;
    char     buf[CMD_SIZE];
    char     pending[CMD_SIZE * 2];
    size_t   plen = 0;
    int      need_prompt = 1;

    pending[0] = '\0';
    send_welcome(s);

    while (s->active && g_server_running) {
        if (need_prompt) {
            send_prompt(s);
            need_prompt = 0;
        }

        int n = session_recv(s, buf, sizeof(buf));
        if (n <= 0) break;

        if (plen + (size_t)n >= sizeof(pending)) {
            plen = 0;
            pending[0] = '\0';
        }
        memcpy(pending + plen, buf, (size_t)n);
        plen += (size_t)n;
        pending[plen] = '\0';

        char *start = pending;
        char *nl;
        int   stop  = 0;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            trim(start);
            if (start[0] != '\0') {
                session_add_history(s, start);
                int rc = execute_command(s, start);
                if (rc < 0) { s->active = 0; stop = 1; break; }
            }
            start = nl + 1;
            need_prompt = 1;
        }

        plen = strlen(start);
        memmove(pending, start, plen + 1);

        if (stop) break;
    }

    log_msg("INFO", "Session %d disconnected (%s)",
            s->id, inet_ntoa(s->addr.sin_addr));

    remove_session(s);
    session_destroy(s);
    return NULL;
}
