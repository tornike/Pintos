#include "vm/mmap.h"
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"


static int get_mapid (void) {
  struct thread *t = thread_current ();
  int id = t->next_free_mapid++;
  
  while(mmap_lookup(&t->mapping_table, t->next_free_mapid) != NULL)
    t->next_free_mapid++;
  return id;
}

void* mmap_allocate (struct file *file, uint8_t *start_addr) {
  struct mmap *m = malloc(sizeof(struct mmap));
  if (m == NULL) PANIC("mmap_allocate: Kernel out of memory!");
  m->mapping = get_mapid();
  m->file = file;

  m->start_addr = start_addr;
  m->end_addr = start_addr;
  return m;
}

void mmap_deallocate (struct mmap *m) {
  struct thread *t = thread_current ();
  uint8_t *page_addr;
  for (page_addr = m->start_addr; page_addr < m->end_addr; page_addr += PGSIZE) {
    struct page *page = page_lookup(&t->sup_page_table, page_addr);
    ASSERT (page != NULL && page->file_info != NULL);
    page_unmap(page);
    page_remove(page);
    page_deallocate(page);
  }

  file_close(m->file);

  t->next_free_mapid = m->mapping < t->next_free_mapid ? m->mapping : t->next_free_mapid;
  free(m);
}

void mmap_remove (struct mmap *m) {
  hash_delete(&thread_current()->mapping_table, &m->elem);
}

/* Returns a hash value for mmap m. */
unsigned mmap_hash (const struct hash_elem *m_, void *aux UNUSED) {
  const struct mmap *m = hash_entry (m_, struct mmap, elem);
  return hash_bytes (&m->mapping, sizeof(m->mapping));
}

/* Returns true if mmap a precedes mmap b. */
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

