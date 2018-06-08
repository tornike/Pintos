#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <inttypes.h>
#include <hash.h>
#include "filesys/file.h"
#include "lib/user/syscall.h"


struct mmap {
    mapid_t mapping;
    struct file *file;
    uint8_t *start_addr;
    uint8_t *end_addr;

    struct hash_elem elem;
};


void* mmap_allocate (struct file*, uint8_t*);
void mmap_deallocate (struct mmap*);
void mmap_remove (struct mmap*);
unsigned mmap_hash (const struct hash_elem*, void* UNUSED);
bool mmap_less (const struct hash_elem*, const struct hash_elem*, void* UNUSED);
struct mmap *mmap_lookup (struct hash*, mapid_t);
void mmap_mapping_table_dest (struct hash_elem*, void*);

#endif