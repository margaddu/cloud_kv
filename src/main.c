#define _GNU_SOURCE

#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5

#include "kv_store.h"
#include "protocol.h"
#include "connection_queue.h"

int keep_going = 1;

pthread_rwlock_t rwlock;
// int writers_waiting = 0; // Don't get reader locks if writers are waiting?
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char *LOG_FILE = "server.log";

void handle_sigint(int signo) {
    keep_going = 0;
}
//Timestamp/Logging function
static void log_op(const char *op, const char *key, const char *status, const char *value_opt) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    pthread_mutex_lock(&log_mutex);

    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        if (value_opt) {
            fprintf(f, "[%s] %s key=%s value=%s status=%s\n", ts, op, key, value_opt, status);
        } else {
            fprintf(f, "[%s] %s key=%s status=%s\n", ts, op, key, status);
        }
        fclose(f);
    }

    pthread_mutex_unlock(&log_mutex);
}

typedef struct {
    connection_queue_t* queue;
    KvStore* kv_store;
} ThreadArgs;

void *thread_task(void *args) {
    ThreadArgs* thread_args = (ThreadArgs *) args;
    connection_queue_t* queue = thread_args->queue;
    KvStore* kv_store = thread_args->kv_store;

    while (1) {
        int client_fd = connection_queue_dequeue(queue);
        if (client_fd == -1) {
            break;
        }
        request req;

        int parse_cmd = read_request(client_fd, &req);
        if (parse_cmd == -1) {
            respond_invalid_command(client_fd);
            close(client_fd);
            continue;
        }

        if (is_put_request(&req) && parse_cmd== 3){
            pthread_rwlock_wrlock(&rwlock);

            kv_store_put(kv_store, req.key, req.val);

            pthread_rwlock_unlock(&rwlock);
            // kv_store_save_to_disk(kv_store); // Saves to disk for persistence
            log_op("PUT", req.key, "OK", req.val); //Display timestamp for PUT
            // printf("User PUT: Key=%s, Val=%s\n", req.key, req.val);

            respond_success(client_fd);
        }

        else if(is_get_request(&req) && parse_cmd == 2){
            // printf("User GET: Key=%s\n", req.key);
            pthread_rwlock_rdlock(&rwlock);

            char* data = kv_store_get(kv_store, req.key);

            if (data == NULL) {
                log_op("GET", req.key, "NOT_FOUND", NULL); //Display timestamp for GET
                respond_not_found(client_fd);
            } else {
                log_op("GET", req.key, "OK", data); //Display timestamp for GET
                respond_with_payload(client_fd, (uint8_t*)data, strlen(data));
            }

            pthread_rwlock_unlock(&rwlock);
        }
        else if(is_del_request(&req) && parse_cmd == 2){
            // printf("User DEL: Key=%s\n", req.key);

            pthread_rwlock_wrlock(&rwlock);
            int deleted = kv_store_del(kv_store, req.key);
            pthread_rwlock_unlock(&rwlock);

            if(deleted==1){
                // kv_store_save_to_disk(kv_store); // Saves to disk for persistence
                log_op("DEL", req.key, "OK", NULL); //Display timestamp for DEL
                respond_success(client_fd);
            }
            else{
                log_op("DEL", req.key, "NOT_FOUND", NULL); //Display timestamp for DEL
                respond_not_found(client_fd);
            }
        }

        else{
            respond_invalid_command(client_fd);
        }

        close(client_fd);
    }
    return NULL;
}

int main(int argc, char **argv) {
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_flags = 0;
    sa_int.sa_handler = handle_sigint;

    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    sigset_t all_signals, old_set;
    sigfillset(&all_signals);
    if (sigprocmask(SIG_BLOCK, &all_signals, &old_set) == -1) {
        perror("sigprocmask");
        return 1;
    }
  
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    const char *port = argv[1];
    struct addrinfo hints;
    memset(&hints,0,sizeof(hints));
    hints.ai_family= AF_UNSPEC;
    hints.ai_socktype= SOCK_STREAM;
    hints.ai_flags= AI_PASSIVE;
    struct addrinfo *servers;

    if ((getaddrinfo(NULL,port, &hints,&servers)) != 0) {
        fprintf(stderr, "getaddrinfo error");
        return -1;
    }
    int sock_fd;
    if ((sock_fd = socket(servers->ai_family, servers->ai_socktype, 0)) == -1) {
        perror("socket");
        freeaddrinfo(servers);
        return -1;
    }
    if(bind(sock_fd,servers->ai_addr,servers->ai_addrlen)==-1){
        perror("bind");
        freeaddrinfo(servers);
        close(sock_fd);
        return -1;
    }
    freeaddrinfo(servers);
    if(listen(sock_fd,LISTEN_QUEUE_LEN)==-1){
        perror("listen");
        close(sock_fd);
        return -1;

    }

    KvStore kv_store = {0};
    kv_store_pull_from_disk(&kv_store); // Loads data from disk if exists

    pthread_rwlock_init(&rwlock, NULL);

    connection_queue_t *queue = malloc(sizeof(connection_queue_t));
    if (queue == NULL) {
        perror("malloc");
        close(sock_fd);
        kv_store_free(&kv_store);
        return 1;
    }

    if (connection_queue_init(queue) == -1) {
        perror("connection_queue");
        close(sock_fd);
        free(queue);
        kv_store_free(&kv_store);
        return 1;
    }

    pthread_t threads[N_THREADS];

    ThreadArgs *thread_args = malloc(sizeof(ThreadArgs));
    if (thread_args == NULL) {
        perror("malloc");
        close(sock_fd);
        connection_queue_free(queue);
        free(queue);
        kv_store_free(&kv_store);
        return 1;
    }

    thread_args->queue = queue;
    thread_args->kv_store = &kv_store;

    for (int i = 0; i < N_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_task, thread_args) == -1) {
            connection_queue_shutdown(queue);
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            perror("pthread_create");
            close(sock_fd);
            connection_queue_free(queue);
            kv_store_free(&kv_store);
            free(queue);
            free(thread_args);
            return 1;
        }
    }

    if (sigprocmask(SIG_SETMASK, &old_set, NULL) == -1) {
        perror("sigprocmask");
        close(sock_fd);
        connection_queue_shutdown(queue);

        for (int i = 0; i < N_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        connection_queue_free(queue);
        kv_store_free(&kv_store);
        free(queue);
        free(thread_args);
        return 1;
    }

    printf("KV-Store Server started on port %s\n", port);
    printf("Ready for connections...\n");
    while(keep_going==1){   // Main Logic for PUT, GET, Everything above is the base for building a TCP server
        int client_fd= accept(sock_fd,NULL,NULL);
        if(client_fd==-1){
            if(errno==EINTR){
                continue;
            }
            connection_queue_shutdown(queue);
            for (int i = 0; i < N_THREADS; i++) {
                pthread_join(threads[i], NULL);
            }
            connection_queue_free(queue);
            perror("accept");
            kv_store_free(&kv_store);
            pthread_rwlock_destroy(&rwlock);
            close(sock_fd);
            return -1;
        }

        if (connection_queue_enqueue(queue, client_fd) == -1) {
            close(client_fd);
            break;
        }
    }

    kv_store_free(&kv_store);

    if (connection_queue_shutdown(queue) == -1) {
        perror("connection_queue_shutdown");
        free(thread_args);
        close(sock_fd);
        return 1;
    }

    for (int i = 0; i < N_THREADS; i++) {
        if (pthread_join(threads[i], NULL) == -1) {
            perror("pthread_join");
            connection_queue_free(queue);
            free(queue);
            free(thread_args);
            close(sock_fd);
            return 1;
        }
    }

    if (connection_queue_free(queue) == -1) {
        perror("connection_queue_free");
        free(queue);
        free(thread_args);
        close(sock_fd);
        return 1;
    }

    if (close(sock_fd) == -1) {
        perror("close");
        free(thread_args);
        free(queue);
        return 1;
    }

    free(thread_args);
    free(queue);
    pthread_rwlock_destroy(&rwlock);
    return 0;
}

