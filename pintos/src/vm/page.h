#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <inttypes.h>
#include "vm/frame.h"
#include "filesys/file.h"
#include <hash.h>


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


bool load_page (struct page*);



#endif
