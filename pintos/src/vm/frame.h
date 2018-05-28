#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/page.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/synch.h"


struct list frame_table;
struct lock frame_lock;

struct frame {
  uint8_t *p_addr; /* Physical address of the page */
  struct page *u_page; /* Pointer to user suplemental page */
  bool pinned;

  struct list_elem elem;
};


void frame_init(void);
struct frame* frame_allocate (struct page*);
void frame_deallocate (struct frame*);


#endif