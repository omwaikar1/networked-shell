/*
 * client.c - Networked Concurrent Shell client.
 *
 * Author : Samruddhi Navale
 * Role   : TCP client that connects to the NCS server.  A dedicated
 *          reader thread prints anything that arrives from the server
 *          (banner, prompts, command output), while the main thread
 *          reads lines from stdin and forwards them to the server.
 *          Two threads = concurrent send / receive without blocking
 *          on each other.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

/* ------------------------------------------------------------------ */
/* Process-wide state                                                  */
/* ------------------------------------------------------------------ */
static int            g_sock    = -1;
static volatile int   g_running = 1;

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <host> [port]\n"
        "  Connect to a Networked Concurrent Shell server.\n"
        "  Default port: %d\n",
        prog, DEFAULT_PORT);
    exit(EXIT_FAILURE);
}

/* ------------------------------------------------------------------ */
/* Reader thread - copies server -> stdout                             */
/* ------------------------------------------------------------------ */
static void *reader_thread(void *arg)
{
    (void)arg;
    char buf[BUF_SIZE];

    while (g_running) {
        ssize_t n = recv(g_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            g_running = 0;
            break;
        }
        buf[n] = '\0';
        fputs(buf, stdout);
        fflush(stdout);
    }
    fprintf(stdout, "\n[disconnected from server]\n");
    fflush(stdout);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Networking                                                          */
/* ------------------------------------------------------------------ */
static int connect_server(const char *host, int port)
{
    struct addrinfo  hints = {0};
    struct addrinfo *res   = NULL;
    char             port_str[16];

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%d", port);

    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "Cannot resolve host %s: %s\n",
                host, gai_strerror(gai));
        return -1;
    }

    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }
    if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(s);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return s;
}

/* ------------------------------------------------------------------ */
/* Signal handling                                                     */
/* ------------------------------------------------------------------ */
static void handle_sigint(int sig)
{
    (void)sig;
    g_running = 0;
    if (g_sock >= 0) shutdown(g_sock, SHUT_RDWR);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc < 2) usage(argv[0]);

    const char *host = argv[1];
    int         port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return EXIT_FAILURE;
    }

    /* Be friendly with disconnects. */
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    g_sock = connect_server(host, port);
    if (g_sock < 0) return EXIT_FAILURE;
    fprintf(stderr, "[connected to %s:%d]\n", host, port);

    pthread_t reader;
    if (pthread_create(&reader, NULL, reader_thread, NULL) != 0) {
        perror("pthread_create");
        close(g_sock);
        return EXIT_FAILURE;
    }

    char line[CMD_SIZE];
    while (g_running && fgets(line, sizeof(line), stdin)) {
        size_t  len  = strlen(line);
        if (len == 0) continue;

        ssize_t sent = send(g_sock, line, len, 0);
        if (sent <= 0) break;

        /* Detect explicit disconnect so we don't hang on the next fgets. */
        if (strncmp(line, "exit", 4) == 0 ||
            strncmp(line, "quit", 4) == 0) {
            usleep(150000);             /* let the goodbye line arrive */
            break;
        }
    }

    g_running = 0;
    shutdown(g_sock, SHUT_RDWR);
    pthread_join(reader, NULL);
    close(g_sock);
    return 0;
}
