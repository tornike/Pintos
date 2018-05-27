#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <inttypes.h>
#include "vm/frame.h"
#include "filesys/file.h"
#include <hash.h>
#include "lib/user/syscall.h"


struct file_info {
    struct file *file;
    off_t offset;
    off_t length;
};

struct page {
    uint8_t *v_addr; /* User space page address. */
    struct frame *frame; /* Pointer to the frame which this page holds. */
    bool writable;
    struct file_info *file_info;
    // swap something

    struct hash_elem elem;
};

unsigned page_hash (const struct hash_elem*, void* UNUSED);
bool page_less (const struct hash_elem*, const struct hash_elem*, void* UNUSED);

struct mmap {
    mapid_t mapping;
    struct file *file;
    void *start_addr;
    void *end_addr;

    struct hash_elem elem;
};

int page_get_mapid (void);

unsigned mmap_hash (const struct hash_elem*, void* UNUSED);
bool mmap_less (const struct hash_elem*, const struct hash_elem*, void* UNUSED);


bool load_page (struct page*);



#endif
