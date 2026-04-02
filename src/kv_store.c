#include "kv_store.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

unsigned int hash(const char *key){
    unsigned int hash_val=0;
    for(int i=0; key[i]!='\0' ;i++){
        hash_val+= ((hash_val<<5)+ hash_val) + key[i];
    }
    return hash_val % TABLE_SIZE;
}

char* kv_store_get(KvStore* kv_store, const char* key) {
    unsigned int idx= hash(key);
    Node *curr= kv_store->buckets[idx];
    while (curr != NULL) {
        if (strcmp(curr->key, key) == 0) {
            return curr->val;
        }
        curr = curr->next;
    }
    return NULL;
}

void kv_store_put(KvStore* kv_store, const char* key, const char* val) {
    unsigned int idx= hash(key);
    Node* curr= kv_store->buckets[idx];
    int checked=0;

    //Check for same key name if it already exists we override
    while(curr!=NULL){
        if (strcmp(curr->key,key)==0){
            free(curr->val);
            curr->val= strdup(val);
            checked=1;
            break;
        }
        curr= curr->next;
    }

    if(checked!=1){
        Node * new_kv= malloc(sizeof(Node));
        new_kv->key=strdup(key); //Creates permanent copies of the data
        new_kv->val=strdup(val); //Creates permanent copies of the data
        new_kv->next= kv_store->buckets[idx]; //Points next to whatever is already at idx, so every new collision is at the beginning of the linked list.
        kv_store->buckets[idx]= new_kv;
    }
}

int kv_store_del(KvStore* kv_store, const char* key) {
    unsigned int idx= hash(key);
    Node *curr= kv_store->buckets[idx];
    Node *prev= NULL;
    int deleted=0;

    while(curr!=NULL){
        if(strcmp(curr->key,key)==0){
            if(prev==NULL){
                kv_store->buckets[idx]= curr->next;
            }
            else{
                prev->next= curr->next;
            }
            free(curr->key);
            free(curr->val);
            free(curr);
            deleted=1;
            break;
        }
        prev= curr;
        curr= curr->next;
    }

    return deleted;
}

void kv_store_save_to_disk(KvStore* kvStore){     // Saves the current contents of the kv_store into a database.txt file
    FILE *f= fopen("database.txt", "w");

    if(f==NULL){
        perror("Failed to open Database file.");
    }

    for(int i=0; i<TABLE_SIZE; i++){
        Node *curr= kvStore->buckets[i];
        while(curr!=NULL){
            fprintf(f, "%s %s\n", curr->key, curr->val);
            curr=curr->next;
        }
    }
    fclose(f);
}

void kv_store_pull_from_disk(KvStore* kvStore){  //Reloads the kv_store with the saved values from database.txt
    FILE *f= fopen("database.txt", "r");
    if(f==NULL){
        return;
    }

    char key[30];
    char val[100];

    while (fscanf(f, "%s %s",key, val) == 2){
        kv_store_put(kvStore, key, val);
    }
    fclose(f);
    printf("Loaded data from disk.\n");
}

void kv_store_free(KvStore *kv_store) {
    if (kv_store == NULL) {
        return;
    }

    for (int i = 0; i < TABLE_SIZE; i++) {
        Node *curr = kv_store->buckets[i];
        
        while (curr != NULL) {
            Node *next = curr->next;
            
            free(curr->key);
            free(curr->val);
            
            free(curr);
            
            curr = next;
        }
        
        kv_store->buckets[i] = NULL; 
    }
}
