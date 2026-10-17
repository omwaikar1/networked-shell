/*
 * server.c - Networked Concurrent Shell (NCS) server.
 *
 * Author : Om Waikar
 * Role   : Server core, threading, connection management.
 *
 * The server opens a TCP listening socket, accepts one client at a time,
 * allocates a Session object, registers it in a fixed-size slot table
 * protected by a mutex, and spawns a detached pthread that runs the
 * session loop in session.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "session.h"
#include "utils.h"

/* ------------------------------------------------------------------ */
/* Globals declared extern in session.h                                */
/* ------------------------------------------------------------------ */
Session         *g_sessions[MAX_CLIENTS] = {0};
pthread_mutex_t  g_sessions_lock         = PTHREAD_MUTEX_INITIALIZER;
volatile int     g_server_running        = 1;

/* File-scope: the listening fd, so the signal handler can close it. */
static int       g_listen_fd             = -1;

/* ------------------------------------------------------------------ */
/* Signal handling - clean shutdown on Ctrl-C or SIGTERM.              */
/* ------------------------------------------------------------------ */
static void handle_sigint(int sig)
{
    (void)sig;
    g_server_running = 0;
    if (g_listen_fd >= 0) {
        /* Wake the blocking accept() so the main loop can exit. */
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Session table - simple O(N) allocator into a fixed slot array.      */
/* ------------------------------------------------------------------ */
static int add_session(Session *s)
{
    int slot = -1;
    pthread_mutex_lock(&g_sessions_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_sessions[i] == NULL) {
            g_sessions[i] = s;
            s->id         = i + 1;
            slot          = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_sessions_lock);
    return slot;
}

/* Called from the worker thread (or shutdown path) when a client goes
 * away.  Public so session.c can reach it through session.h.          */
void remove_session(Session *s)
{
    pthread_mutex_lock(&g_sessions_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_sessions[i] == s) {
            g_sessions[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_sessions_lock);
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */
static int parse_port(int argc, char **argv)
{
    if (argc < 2) return DEFAULT_PORT;
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    return port;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [port]\n"
        "  Starts the Networked Concurrent Shell server.\n"
        "  Default port: %d\n",
        prog, DEFAULT_PORT);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        return 0;
    }

    int port = parse_port(argc, argv);

    /* Install signal handlers. */
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);   /* don't die when a client drops mid-write */

    /* Create + bind + listen. */
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return EXIT_FAILURE;
    }
    if (listen(g_listen_fd, 16) < 0) {
        perror("listen");
        return EXIT_FAILURE;
    }

    log_msg("INFO", "NCS server listening on 0.0.0.0:%d (max clients=%d)",
            port, MAX_CLIENTS);

    /* Accept loop. */
    while (g_server_running) {
        struct sockaddr_in caddr;
        socklen_t          clen = sizeof(caddr);

        int cfd = accept(g_listen_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EINTR || !g_server_running) break;
            perror("accept");
            continue;
        }

        Session *s = session_create(cfd, caddr);
        if (!s) {
            log_msg("ERROR", "session_create failed (out of memory?)");
            close(cfd);
            continue;
        }

        if (add_session(s) < 0) {
            const char *busy =
                "Server is full, please try again later.\n";
            send(cfd, busy, strlen(busy), 0);
            session_destroy(s);
            continue;
        }

        if (pthread_create(&s->thread, NULL, session_thread, s) != 0) {
            log_msg("ERROR", "pthread_create failed for client %s",
                    inet_ntoa(caddr.sin_addr));
            remove_session(s);
            session_destroy(s);
            continue;
        }
        pthread_detach(s->thread);

        log_msg("INFO", "Client connected: id=%d ip=%s port=%d",
                s->id, inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
    }

    /* Shutdown: poke every live session so they unblock and exit. */
    log_msg("INFO", "Shutting down, closing %d possible sessions",
            MAX_CLIENTS);
    pthread_mutex_lock(&g_sessions_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_sessions[i]) {
            shutdown(g_sessions[i]->sockfd, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&g_sessions_lock);

    /* Give worker threads a moment to wrap up before main returns. */
    sleep(1);
    log_msg("INFO", "Server stopped cleanly");
    return 0;
}
