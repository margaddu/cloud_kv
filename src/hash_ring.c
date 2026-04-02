#include "hash_ring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fnv1a_hash(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (unsigned char)(*str++);
        hash *= 16777619;
    }
    return hash;
}

static int compare_ring_nodes(const void *a, const void *b) {
    const RingNode *na = (const RingNode *)a;
    const RingNode *nb = (const RingNode *)b;

    if (na->hash < nb->hash) return -1;
    if (na->hash > nb->hash) return 1;
    return 0;
}

void hash_ring_init(HashRing *ring) {
    ring->count = 0;
}

int hash_ring_build(HashRing *ring, const char *servers[], int num_servers) {
    hash_ring_init(ring);

    if (num_servers <= 0 || num_servers > MAX_PHYSICAL_NODES) {
        return -1;
    }

    for (int i = 0; i < num_servers; i++) {
        for (int v = 0; v < VIRTUAL_NODES; v++) {
            if (ring->count >= MAX_RING_NODES) {
                return -1;
            }

            char vnode_label[128];
            snprintf(vnode_label, sizeof(vnode_label), "%s#%d", servers[i], v);

            ring->nodes[ring->count].hash = fnv1a_hash(vnode_label);
            snprintf(ring->nodes[ring->count].server_addr,
                     sizeof(ring->nodes[ring->count].server_addr),
                     "%s",
                     servers[i]);
            ring->count++;
        }
    }

    qsort(ring->nodes, ring->count, sizeof(RingNode), compare_ring_nodes);
    return 0;
}

const char *hash_ring_get_server(HashRing *ring, const char *key) {
    if (ring == NULL || ring->count == 0 || key == NULL) {
        return NULL;
    }

    uint32_t key_hash = fnv1a_hash(key);

    for (int i = 0; i < ring->count; i++) {
        if (key_hash <= ring->nodes[i].hash) {
            return ring->nodes[i].server_addr;
        }
    }

    return ring->nodes[0].server_addr;
}