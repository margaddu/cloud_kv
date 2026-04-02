#ifndef HASH_RING_H
#define HASH_RING_H

#include <stdint.h>

#define MAX_PHYSICAL_NODES 3
#define VIRTUAL_NODES 20
#define MAX_RING_NODES (MAX_PHYSICAL_NODES * VIRTUAL_NODES)

typedef struct {
    uint32_t hash;
    char server_addr[64];
} RingNode;

typedef struct {
    RingNode nodes[MAX_RING_NODES];
    int count;
} HashRing;

void hash_ring_init(HashRing *ring);
int hash_ring_build(HashRing *ring, const char *servers[], int num_servers);
const char *hash_ring_get_server(HashRing *ring, const char *key);

#endif