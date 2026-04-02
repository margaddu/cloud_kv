#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hash_ring.h"

#define ROUTER_PORT "5980"
#define BUFFER_SIZE 512
#define LISTEN_QUEUE_LEN 10

typedef struct {
    int client_fd;
    HashRing *ring;
} ThreadArgs;

static int extract_key(const char *request_buf, char *key_out, size_t key_out_size) {
    char cmd[32];
    char key[256];
    int parsed = sscanf(request_buf, "%31s %255s", cmd, key);

    if (parsed < 2) {
        return -1;
    }

    snprintf(key_out, key_out_size, "%s", key);
    return 0;
}

static int parse_server_addr(const char *server_addr, char *ip_out, size_t ip_out_size,
                             char *port_out, size_t port_out_size) {
    const char *colon = strchr(server_addr, ':');
    if (colon == NULL) {
        return -1;
    }

    size_t ip_len = (size_t)(colon - server_addr);
    if (ip_len == 0 || ip_len >= ip_out_size) {
        return -1;
    }

    memcpy(ip_out, server_addr, ip_len);
    ip_out[ip_len] = '\0';

    snprintf(port_out, port_out_size, "%s", colon + 1);
    return 0;
}

static int connect_to_backend(const char *ip, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    int backend_fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip, port, &hints, &result) != 0) {
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        backend_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (backend_fd == -1) {
            continue;
        }

        if (connect(backend_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(backend_fd);
        backend_fd = -1;
    }

    freeaddrinfo(result);
    return backend_fd;
}

static int forward_request_to_backend(const char *server_addr, const char *request_buf,
                                      char *response_buf, size_t response_buf_size) {
    char ip[64];
    char port[16];

    if (parse_server_addr(server_addr, ip, sizeof(ip), port, sizeof(port)) == -1) {
        return -1;
    }

    int backend_fd = connect_to_backend(ip, port);
    if (backend_fd == -1) {
        return -1;
    }

    size_t req_len = strlen(request_buf);
    if (send(backend_fd, request_buf, req_len, 0) < 0) {
        close(backend_fd);
        return -1;
    }

    ssize_t bytes_read = recv(backend_fd, response_buf, response_buf_size - 1, 0);
    if (bytes_read < 0) {
        close(backend_fd);
        return -1;
    }

    response_buf[bytes_read] = '\0';
    close(backend_fd);
    return 0;
}

static void *handle_client(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    int client_fd = args->client_fd;
    HashRing *ring = args->ring;

    char request_buf[BUFFER_SIZE];
    memset(request_buf, 0, sizeof(request_buf));

    ssize_t bytes_read = recv(client_fd, request_buf, sizeof(request_buf) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        free(args);
        return NULL;
    }

    request_buf[bytes_read] = '\0';

    char key[256];
    if (extract_key(request_buf, key, sizeof(key)) == -1) {
        const char *err = "INVALID COMMAND!\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        free(args);
        return NULL;
    }

    const char *target_server = hash_ring_get_server(ring, key);
    if (target_server == NULL) {
        const char *err = "ROUTER ERROR\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        free(args);
        return NULL;
    }

    char response_buf[BUFFER_SIZE];
    memset(response_buf, 0, sizeof(response_buf));

    if (forward_request_to_backend(target_server, request_buf, response_buf, sizeof(response_buf)) == -1) {
        const char *err = "BACKEND UNAVAILABLE\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        free(args);
        return NULL;
    }

    send(client_fd, response_buf, strlen(response_buf), 0);
    close(client_fd);
    free(args);
    return NULL;
}

int main(void) {
    const char *servers[] = {
        "127.0.0.1:5981",
        "127.0.0.1:5982",
        "127.0.0.1:5983"
    };

    HashRing ring;
    if (hash_ring_build(&ring, servers, 3) == -1) {
        fprintf(stderr, "Failed to build hash ring\n");
        return 1;
    }

    struct addrinfo hints;
    struct addrinfo *router_info = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, ROUTER_PORT, &hints, &router_info) != 0) {
        fprintf(stderr, "getaddrinfo failed for router\n");
        return 1;
    }

    int router_fd = socket(router_info->ai_family, router_info->ai_socktype, router_info->ai_protocol);
    if (router_fd == -1) {
        perror("socket");
        freeaddrinfo(router_info);
        return 1;
    }

    int opt = 1;
    setsockopt(router_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(router_fd, router_info->ai_addr, router_info->ai_addrlen) == -1) {
        perror("bind");
        close(router_fd);
        freeaddrinfo(router_info);
        return 1;
    }

    freeaddrinfo(router_info);

    if (listen(router_fd, LISTEN_QUEUE_LEN) == -1) {
        perror("listen");
        close(router_fd);
        return 1;
    }

    printf("Router started on port %s\n", ROUTER_PORT);

    while (1) {
        int client_fd = accept(router_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        pthread_t thread;
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        if (args == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }

        args->client_fd = client_fd;
        args->ring = &ring;

        if (pthread_create(&thread, NULL, handle_client, args) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(args);
            continue;
        }

        pthread_detach(thread);
    }

    close(router_fd);
    return 0;
}
