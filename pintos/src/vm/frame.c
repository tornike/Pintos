#include "vm/frame.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h" // filesys_lock

struct list frame_table;
struct lock frame_lock;

void frame_init() {
  list_init(&frame_table);
  lock_init(&frame_lock);
}

static struct frame* eviction(struct page *);

struct frame* frame_allocate (struct page *u_page) {
  struct frame *frame = NULL;
  void *k_page = palloc_get_page (PAL_USER);
  if (k_page == NULL) { 
    frame = eviction(u_page);
  } else {
    frame = malloc(sizeof(struct frame));
    if (frame == NULL) return NULL;
    
    frame->p_addr = k_page;
    frame->pinned = false;

    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &frame->elem);
    lock_release(&frame_lock);
  }
  frame->u_page = u_page;
  return frame;
}

void frame_deallocate (struct frame *frame) {
  palloc_free_page(frame->p_addr);

  lock_acquire(&frame_lock);
  list_remove(&frame->elem);
  lock_release(&frame_lock);

  free(frame);
}


static struct frame*
eviction(struct page *temp_page) 
{ 
  lock_acquire(&frame_lock);
  while(true) {
    struct list_elem *e = list_begin(&frame_table);
    while (e != list_end(&frame_table)) {
      struct frame *frame = list_entry (e, struct frame, elem);
      struct page *u_page = frame->u_page;
      if (!pagedir_is_accessed(u_page->pagedir, u_page->v_addr)) { // Swap out
        u_page->frame = NULL;
        pagedir_clear_page(u_page->pagedir, u_page->v_addr); // Remove page from page directory
        //if (u_page->file_info != NULL) { // Check if it's file page
          //if(pagedir_is_dirty(u_page->pagedir, u_page->v_addr)) { // write back to file or discard page
          // lock_acquire(&filesys_lock);
          // file_seek(u_page->file_info->file, u_page->file_info->offset);
          // file_write(u_page->file_info->file, frame->p_addr, u_page->file_info->length);
          // lock_release(&filesys_lock);
          //}
        //} else { // swap
          u_page->swap_slot = swap_out(frame->p_addr);
          //printf("SWAP OUT: %u\n", u_page->v_addr);
        //}
        frame->u_page = temp_page;
        lock_release(&frame_lock);
        return frame;
      }
      pagedir_set_accessed(u_page->pagedir, u_page->v_addr, false);
      e = list_next(e);
    }
  }
  NOT_REACHED();
}