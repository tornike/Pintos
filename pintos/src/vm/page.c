#include "vm/page.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include <string.h>
#include "threads/malloc.h"
#include "userprog/syscall.h" // for filesys_lock
#include <stdio.h>
#include "userprog/pagedir.h"

/* 
 * Allocates suplemental page and adds it to suplemental page table.
 * returns NULL if allocations failed.
 */
struct page *page_allocate (uint8_t *v_addr, bool writable, struct file_info *file_info) {
  struct page *page = malloc(sizeof(struct page));
  if (page == NULL) return NULL;
  
  /* Prepare virtual page */
  page->v_addr = v_addr;
  page->frame = NULL;
  page->writable = writable;
  page->file_info = file_info;

  hash_insert(&thread_current()->sup_page_table, &page->elem);
  return page;
}

void page_deallocate (struct page *page) {
  pagedir_clear_page(thread_current()->pagedir, page->v_addr);
  if (page->frame != NULL)
    frame_deallocate(page->frame);
  if (page->file_info != NULL)
    free(page->file_info);
  free(page);
}

/* Remove page from suplemental page table */
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

void page_unmap(struct page *page) {
  if (page->file_info == NULL) return;
  if (page->frame != NULL && pagedir_is_dirty(thread_current()->pagedir, page->v_addr)) {
    lock_acquire(&filesys_lock);
    file_seek(page->file_info->file, page->file_info->offset);
    file_write(page->file_info->file, page->frame->p_addr, page->file_info->length);
    lock_release(&filesys_lock);
  }
}


void mmap_deallocate (struct mmap *m) {
  uint8_t *page_addr;
  for (page_addr = m->start_addr; page_addr < m->end_addr; page_addr += PGSIZE) {
    struct page *page = page_lookup(&thread_current()->sup_page_table, page_addr);
    if (page == NULL || page->file_info == NULL) printf("BUG!!!!!!!\n");
    page_unmap(page);
    page_remove(page);
    page_deallocate(page);
   }

  lock_acquire(&filesys_lock);
  file_close(m->file);
  lock_release(&filesys_lock);

  free(m);
}

void mmap_remove (struct mmap *m) {
  hash_delete(&thread_current()->mapping_table, &m->elem);
}

int next_free_mapid = 0;

int page_get_mapid () {
  return next_free_mapid++;
}

/* Returns a hash value for mmap m. */
unsigned mmap_hash (const struct hash_elem *m_, void *aux UNUSED) {
  const struct mmap *m = hash_entry (m_, struct mmap, elem);
  return hash_bytes (&m->mapping, sizeof(m->mapping));
}

/* Returns true if mmap a precedes page b. */
bool mmap_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) { 
  const struct mmap *a = hash_entry (a_, struct mmap, elem);
  const struct mmap *b = hash_entry (b_, struct mmap, elem);

  return a->mapping < b->mapping;  
}

/* Returns the mmap containing the given mapping,
   or a null pointer if no such mmap exists. */
struct mmap *
mmap_lookup (struct hash *mmaps, mapid_t mapping)
{
  struct mmap m;
  struct hash_elem *e;

  m.mapping = mapping;
  e = hash_find (mmaps, &m.elem);
  return e != NULL ? hash_entry (e, struct mmap, elem) : NULL;
}

void mmap_mapping_table_dest (struct hash_elem *elem, void *aux UNUSED) {
  struct mmap *m = hash_entry (elem, struct mmap, elem);
  mmap_deallocate(m);
}


/* page_load helpers */

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
void page_load (struct page *sup_page) {
  ASSERT(sup_page->frame == NULL);

  struct frame *frame = frame_allocate(sup_page);
  if (frame == NULL) { printf("Page Load: frame is null\n"); thread_exit(); }

  if(!install_page (sup_page->v_addr, frame->p_addr, sup_page->writable)) { printf("Page Load: page install\n"); thread_exit(); }

  if (sup_page->file_info != NULL) { /* File page. */
    lock_acquire(&filesys_lock);
    file_seek(sup_page->file_info->file, sup_page->file_info->offset);
    off_t read_size = file_read(sup_page->file_info->file, frame->p_addr, sup_page->file_info->length);
    lock_release(&filesys_lock);
    if (read_size != sup_page->file_info->length) { printf("Page Load: read size\n"); thread_exit(); }
    memset (frame->p_addr + read_size, 0, PGSIZE - read_size);
  } else {
    memset (frame->p_addr, 0, PGSIZE); /* Just zeroe page. */
  }
}

