#include "vm/frame.h"
#include "userprog/pagedir.h"
#include <stdio.h>


void frame_init() {
    list_init(&frame_table);
    lock_init(&frame_lock);
}


static bool install_page (void *upage, void *kpage, bool writable);


bool frame_allocate (struct frame *frame, enum palloc_flags flags) {
    bool result = false;
    frame->p_addr = palloc_get_page (flags);
    lock_acquire(&frame_lock);
    if (frame->p_addr == NULL) { printf("NO SPACEEEE\n"); return false; } // no space eviction.
    result = install_page (frame->u_page->v_addr, frame->p_addr, frame->u_page->writable);
    lock_release(&frame_lock);
    if (!result) printf("Install page failed\n");
    return result;
}



/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
