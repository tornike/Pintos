#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <inttypes.h>
#include <hash.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"

/* Suplemental page table */

struct file_info {
    struct file *file;
    off_t offset;
    off_t length;
};

struct page {
    uint8_t *v_addr;        /* User space page address. */
    struct frame *frame;    /* Pointer to the frame which this page holds. */
    uint32_t *pagedir;      /* Page directory. */
    bool writable;
    struct file_info *file_info;
    swap_slot_t swap_slot;

    struct hash_elem elem;
};

struct page *page_allocate (uint8_t*, bool, struct file_info*);
void page_deallocate (struct page*);
void page_remove (struct page*);
unsigned page_hash (const struct hash_elem*, void* UNUSED);
bool page_less (const struct hash_elem*, const struct hash_elem*, void* UNUSED);
struct page *page_lookup (struct hash*, void *);
void page_unmap(struct page*);
bool page_load (struct page*);
void page_suplemental_table_dest (struct hash_elem*, void*);


#endif

