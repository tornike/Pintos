#include "vm/page.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include <string.h>
#include "threads/malloc.h"
#include "userprog/syscall.h" // for filesys_lock

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, elem);
  return hash_bytes (&p->v_addr, sizeof(p->v_addr));
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, elem);
  const struct page *b = hash_entry (b_, struct page, elem);

  return a->v_addr < b->v_addr;
}


bool load_page (struct page *sup_page) {
    ASSERT(sup_page->frame == NULL);

    struct frame *frame = malloc(sizeof(struct frame));
    if (frame == NULL) return false;
    frame->u_page = sup_page;
    
    if(!frame_allocate(frame, PAL_USER)) {
        free(frame);
        return false;
    }
    list_push_back(&frame_table, &frame->elem);
    sup_page->frame = frame;

    if (sup_page->file_info != NULL) { /* File page. */
        lock_acquire(&filesys_lock);
        file_seek(sup_page->file_info->file, sup_page->file_info->offset);
        off_t read_size = file_read(sup_page->file_info->file, sup_page->frame->p_addr, sup_page->file_info->length);
        lock_release(&filesys_lock);
        
        if (read_size != sup_page->file_info->length) return false;
        memset (sup_page->frame->p_addr + read_size, 0, PGSIZE - read_size);
        return true;
    } else {
        memset (sup_page->frame->p_addr, 0, PGSIZE); /* Just zeroe page. */
        return true;
    }
}




