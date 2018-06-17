#include "vm/page.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <string.h>
#include "threads/malloc.h"
#include "userprog/syscall.h" // for filesys_lock
#include "userprog/pagedir.h"

/* 
 * Allocates suplemental page and adds it to suplemental page table.
 * returns NULL if allocations failed.
 */
struct page *page_allocate (uint8_t *v_addr, bool writable, struct file_info *file_info) {
  struct page *page = malloc(sizeof(struct page));
  if (page == NULL) PANIC("page_allocate: Kernel out of memory!");

  struct thread *curr = thread_current();
  
  /* Prepare virtual page */
  page->v_addr = v_addr;
  page->frame = NULL;
  page->pagedir = curr->pagedir;
  page->writable = writable;
  page->file_info = file_info;
  page->swap_slot = -1;

  hash_insert(&curr->sup_page_table, &page->elem);
  return page;
}

/* 
 * Marks supplemental page address as not present, 
 * Deallocates it and all it's resources.
 * 
 * Before deallocating, this page must be removed from hash table.
 */
void page_deallocate (struct page *page) {
  pagedir_clear_page(page->pagedir, page->v_addr);
  if (page->frame != NULL)
    frame_deallocate(page->frame);
  if (page->file_info != NULL)
    free(page->file_info);
  free(page);
}

/* Remove page from supplemental page table */
void page_remove (struct page *page) {
  hash_delete(&thread_current()->sup_page_table, &page->elem);
}

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

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct page *
page_lookup (struct hash *pages, void *address)
{
  struct page p;
  struct hash_elem *e;

  p.v_addr = address;
  e = hash_find (pages, &p.elem);
  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

/* Write data back to file if page is dirty */
void page_unmap(struct page *page) {
  ASSERT (page->file_info != NULL);
  if (page->frame != NULL && pagedir_is_dirty(page->pagedir, page->v_addr)) {
    lock_acquire(&filesys_lock);
    file_seek(page->file_info->file, page->file_info->offset);
    file_write(page->file_info->file, page->frame->p_addr, page->file_info->length);
    lock_release(&filesys_lock);
  }
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

/*
 * Loads content of the virtual page in memory.
 * Exits current process if loading fails.
 */
bool page_load (struct page *sup_page) {
  ASSERT(sup_page->frame == NULL);

  struct frame *frame = frame_allocate(sup_page);
  ASSERT (frame != NULL);
  sup_page->frame = frame;

  lock_acquire(&frame_lock);
  sup_page->frame->pinned = true;
  lock_release(&frame_lock);

  if (sup_page->swap_slot != -1) {
    swap_in(sup_page->swap_slot, frame->p_addr);
    sup_page->swap_slot = -1;
  } else if (sup_page->file_info != NULL) { /* File page. */
    //lock_acquire(&filesys_lock);
    file_seek(sup_page->file_info->file, sup_page->file_info->offset);
    off_t read_size = file_read(sup_page->file_info->file, frame->p_addr, sup_page->file_info->length);
    //lock_release(&filesys_lock);
    if (read_size != sup_page->file_info->length) { 
      frame_deallocate(frame);
      sup_page->frame = NULL;
      return false;
    }
    memset (frame->p_addr + read_size, 0, PGSIZE - read_size);
  } else {
    memset (frame->p_addr, 0, PGSIZE); /* Just zeroe page. */
  }

  if(!install_page (sup_page->v_addr, frame->p_addr, sup_page->writable)) { 
    frame_deallocate(frame);
    sup_page->frame = NULL;
    return false;
  }
  sup_page->frame->pinned = false;
  return true;
}

void page_suplemental_table_dest (struct hash_elem *elem, void *aux UNUSED) {
  struct page *p = hash_entry (elem, struct page, elem);
  page_deallocate(p);
}

