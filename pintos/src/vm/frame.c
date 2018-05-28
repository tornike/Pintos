#include "vm/frame.h"
#include "threads/malloc.h"


void frame_init() {
  list_init(&frame_table);
  lock_init(&frame_lock);
}

struct frame* frame_allocate (struct page *u_page) {
  struct frame *frame = malloc(sizeof(struct frame));
  if (frame == NULL) return NULL;
  
  frame->p_addr = palloc_get_page (PAL_USER);
  if (frame->p_addr == NULL) { PANIC("Kernel Bug in frame_allocate: NO SPACE"); }
  frame->u_page = u_page;
  frame->pinned = false;

  u_page->frame = frame;

  list_push_back(&frame_table, &frame->elem);

  return frame;
}

void frame_deallocate (struct frame *frame) {
  palloc_free_page(frame->p_addr);
  list_remove(&frame->elem);
  free(frame);
}

