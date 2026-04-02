#include "connection_queue.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

int connection_queue_init(connection_queue_t *queue) {
    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    queue->shutdown = 0;

    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_mutex_destroy(&queue->lock);
        return -1;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->lock);
        pthread_cond_destroy(&queue->not_full);
        return -1;
    }

    return 0;
}

int connection_queue_enqueue(connection_queue_t *queue, int connection_fd) {
    if (pthread_mutex_lock(&queue->lock) != 0) {
        return -1;
    }

    while (queue->length == CAPACITY && queue->shutdown == 0) {
        if (pthread_cond_wait(&queue->not_full, &queue->lock) != 0) {
            return -1;
        }
    }

    if (queue->shutdown == 1) {
        if (pthread_mutex_unlock(&queue->lock) != 0) {
            return -1;
        }
        return -1;
    }

    queue->client_fds[queue->write_idx] = connection_fd;
    queue->write_idx = (queue->write_idx + 1) % CAPACITY;
    queue->length += 1;

    if (pthread_cond_signal(&queue->not_empty) != 0) {
        return -1;
    }

    if (pthread_mutex_unlock(&queue->lock) != 0) {
        return -1;
    }
    return 0;
}

int connection_queue_dequeue(connection_queue_t *queue) {
    if (pthread_mutex_lock(&queue->lock) != 0) {
        return -1;
    }

    while (queue->length == 0 && queue->shutdown == 0) {
        if (pthread_cond_wait(&queue->not_empty, &queue->lock) != 0) {
            return -1;
        }
    }

    if (queue->length == 0 && queue->shutdown == 1) {
        if (pthread_mutex_unlock(&queue->lock) != 0) {
            return -1;
        }
        return -1;
    }

    int fd = queue->client_fds[queue->read_idx];
    queue->read_idx = (queue->read_idx + 1) % CAPACITY;
    queue->length -= 1;

    if (pthread_cond_signal(&queue->not_full) != 0) {
        return -1;
    }

    if (pthread_mutex_unlock(&queue->lock) != 0) {
        return -1;
    }
    return fd;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    if (pthread_mutex_lock(&queue->lock) != 0) {
        return -1;
    }

    queue->shutdown = 1;

    if (pthread_cond_broadcast(&queue->not_empty) != 0) {
        return -1;
    }
    if (pthread_cond_broadcast(&queue->not_full) != 0) {
        return -1;
    }

    if (pthread_mutex_unlock(&queue->lock) != 0) {
        return -1;
    }

    return 0;
}

int connection_queue_free(connection_queue_t *queue) {
    if (pthread_mutex_destroy(&queue->lock) != 0) {
        return -1;
    }

    if (pthread_cond_destroy(&queue->not_empty) != 0) {
        return -1;
    }
    if (pthread_cond_destroy(&queue->not_full) != 0) {
        return -1;
    }

    return 0;
}
