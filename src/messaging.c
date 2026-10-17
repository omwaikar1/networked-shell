/*
 * messaging.c - Inter-session messaging built-ins.
 *
 * Author : Sakshi Takalkar
 * Role   : Adds `nick`, `who`, `msg <id> <text>` and
 *          `broadcast <text>` so connected clients can talk to
 *          each other across the server.  All cross-session
 *          access is done under g_sessions_lock so the operation
 *          is safe even while other threads are joining/leaving.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "messaging.h"
#include "session.h"
#include "utils.h"

/* ------------------------------------------------------------------ */
/* nick - show or set this session's display name                      */
/* ------------------------------------------------------------------ */
int builtin_nick(Session *s, int argc, char **argv)
{
    if (argc < 2) {
        char line[96];
        snprintf(line, sizeof(line), "nick: %s\n",
                 s->nick[0] ? s->nick : "(unset)");
        session_send(s, line);
        return 0;
    }
    strncpy(s->nick, argv[1], sizeof(s->nick) - 1);
    s->nick[sizeof(s->nick) - 1] = '\0';
    char line[128];
    snprintf(line, sizeof(line), "nick set to '%s'\n", s->nick);
    session_send(s, line);
    return 0;
}

/* ------------------------------------------------------------------ */
/* who - list connected clients with their nicknames                   */
/* ------------------------------------------------------------------ */
int builtin_who(Session *s, int argc, char **argv)
{
    (void)argc; (void)argv;
    pthread_mutex_lock(&g_sessions_lock);
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        Session *p = g_sessions[i];
        if (!p) continue;
        char line[256];
        snprintf(line, sizeof(line),
            "  [%2d] %-16s ip=%s%s\n",
            p->id,
            p->nick[0] ? p->nick : "(no nick)",
            inet_ntoa(p->addr.sin_addr),
            p == s ? "  (you)" : "");
        session_send(s, line);
        count++;
    }
    pthread_mutex_unlock(&g_sessions_lock);

    char tail[64];
    snprintf(tail, sizeof(tail), "Connected: %d\n", count);
    session_send(s, tail);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void join_args(char *out, size_t cap, int argc, char **argv, int start)
{
    out[0] = '\0';
    for (int i = start; i < argc; i++) {
        if (i > start) strncat(out, " ", cap - strlen(out) - 1);
        strncat(out, argv[i], cap - strlen(out) - 1);
    }
}

static void deliver(Session *from, Session *to, const char *kind,
                    const char *text)
{
    char hdr[160];
    const char *who = from->nick[0] ? from->nick : "(anon)";
    snprintf(hdr, sizeof(hdr), "\n*** %s from [%d %s]: ",
             kind, from->id, who);
    session_send(to, hdr);
    session_send(to, text);
    session_send(to, " ***\n");
}

/* ------------------------------------------------------------------ */
/* msg <id> <text...> - private message to one session                 */
/* ------------------------------------------------------------------ */
int builtin_msg(Session *s, int argc, char **argv)
{
    if (argc < 3) {
        session_send(s, "usage: msg <session_id> <text...>\n");
        return 0;
    }
    int target = atoi(argv[1]);
    if (target <= 0) {
        session_send(s, "msg: invalid session id\n");
        return 0;
    }

    char body[CMD_SIZE];
    join_args(body, sizeof(body), argc, argv, 2);

    int delivered = 0;
    pthread_mutex_lock(&g_sessions_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        Session *p = g_sessions[i];
        if (p && p->id == target && p != s) {
            deliver(s, p, "msg", body);
            delivered = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_sessions_lock);

    session_send(s, delivered ? "(sent)\n" : "msg: no such session\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* broadcast <text...> - send to every other session                   */
/* ------------------------------------------------------------------ */
int builtin_broadcast(Session *s, int argc, char **argv)
{
    if (argc < 2) {
        session_send(s, "usage: broadcast <text...>\n");
        return 0;
    }

    char body[CMD_SIZE];
    join_args(body, sizeof(body), argc, argv, 1);

    int delivered = 0;
    pthread_mutex_lock(&g_sessions_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        Session *p = g_sessions[i];
        if (!p || p == s) continue;
        deliver(s, p, "broadcast", body);
        delivered++;
    }
    pthread_mutex_unlock(&g_sessions_lock);

    char tail[64];
    snprintf(tail, sizeof(tail), "(delivered to %d)\n", delivered);
    session_send(s, tail);
    return 0;
}
