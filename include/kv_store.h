#ifndef kv_store_h
#define kv_store_h

#define TABLE_SIZE 500

typedef struct Node {
  char *key;
  char *val;
  struct Node *next; // For collisions to hold more than one node at a specific
                     // array position
} Node;

typedef struct {
  Node *buckets[TABLE_SIZE];
} KvStore;

char *kv_store_get(KvStore *kv_store, const char *key);
void kv_store_put(KvStore *kv_store, const char *key, const char *val);
int kv_store_del(KvStore *kv_store, const char *key);

void kv_store_save_to_disk(KvStore *kvStore);
void kv_store_pull_from_disk(KvStore *kvStore);

void kv_store_free(KvStore *kv_store);

#endif // kv_store_h
