#include "protocol.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 512

int read_exact_bytes(int fd, uint8_t* buffer, size_t bytes) {
    size_t total_bytes = 0;
    while (total_bytes < bytes) {
        size_t remaining = bytes - total_bytes;
        ssize_t bytes_read = recv(fd, buffer + total_bytes, remaining, 0);
        if (bytes_read == 0) {
            // fprintf(stderr, "Error: connection closed\n");
            return -1;
        } else if (bytes_read < 0) {
            // fprintf(stderr, "Error reading from socket\n");
            return -1;
        }

        total_bytes += bytes_read;
    }

    return 0;
}

int read_request(int fd, request* req) {
    char cmd_input[BUFSIZE];

    ssize_t bytes_read = recv(fd, cmd_input, BUFSIZE - 1, 0);

    if (bytes_read <= 0) {
        return -1;
    }

    cmd_input[bytes_read] = '\0';

    int parse_cmd = sscanf(cmd_input, "%s %s %s", req->cmd, req->key, req->val);

    return parse_cmd;
}

int respond_not_found(int fd) {
    char *err_msg= "KEY NOT FOUND!\n";

    write(fd, err_msg, strlen(err_msg));
    return 0;
}

int respond_with_payload(int fd, uint8_t *payload, size_t payload_size) {
    write(fd,payload, payload_size);
    write(fd, "\n", 1);

    return 0;
}

int respond_success(int fd) {
    char *res= "COMPLETE\n";
    write(fd, res, strlen(res));

    return 0;
}


int respond_invalid_command(int fd) {
    char *err_msg= "INVALID COMMAND!\n";
    write(fd, err_msg, strlen(err_msg));

    return 0;
}

int is_get_request(request* req) {
    return strcmp(req->cmd, "GET") == 0;
}

int is_put_request(request* req) {
    return strcmp(req->cmd, "PUT") == 0;
}

int is_del_request(request* req) {
    return strcmp(req->cmd, "DEL") == 0;
}
