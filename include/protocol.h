#ifndef Protocol_h
#define Protocol_h

#include <stdint.h>
#include <stdlib.h>

typedef struct {
  char cmd[5];
  char key[30];
  char val[100];
} request;


int read_request(int fd, request* req);

int is_get_request(request* req);
int is_put_request(request* req);
int is_del_request(request* req);

int respond_with_payload(int fd, uint8_t* payload, size_t payload_size);
int respond_not_found(int fd);
int respond_success(int fd);
int respond_invalid_command(int fd);

#endif // Protocol_h
