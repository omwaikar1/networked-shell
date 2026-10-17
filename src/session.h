/*
 * session.h - Per-client session structure and the thread entry point.
 * Each connected client gets one Session and one worker thread.
 */
#ifndef NCS_SESSION_H
#define NCS_SESSION_H

#include <pthread.h>
#include <netinet/in.h>
#include <time.h>
#include "common.h"

typedef struct {
    int                 id;             /* 1-based session number    */
    int                 sockfd;         /* TCP socket fd              */
    struct sockaddr_in  addr;           /* peer address               */
    char                cwd[PATH_MAX_LEN];          /* isolated cwd  */
    char                history[HISTORY_SIZE][CMD_SIZE];
    int                 history_count;
    int                 active;
    pthread_t           thread;
    char                nick[32];       /* set by Sakshi's `nick` builtin */
    time_t              connected_at;
} Session;

/* Globals owned by server.c, declared here so other modules can see them. */
extern Session          *g_sessions[MAX_CLIENTS];
extern pthread_mutex_t   g_sessions_lock;
extern volatile int      g_server_running;

/* Lifecycle */
Session *session_create(int sockfd, struct sockaddr_in addr);
void     session_destroy(Session *s);

/* Thread entry point used by pthread_create */
void    *session_thread(void *arg);

/* I/O helpers */
int      session_send(Session *s, const char *msg);
int      session_recv(Session *s, char *buf, size_t len);
void     session_add_history(Session *s, const char *cmd);

/* Implemented in server.c so commands.c can list active sessions safely. */
void     remove_session(Session *s);

#endif /* NCS_SESSION_H */
