#include "userprog/files.h"
#include "threads/thread.h"
#include "threads/malloc.h"

#include "userprog/syscall.h" // for filesys_lock

#include "filesys/file.h"
#include "filesys/directory.h"


static int get_fd (void) {
  struct thread *t = thread_current ();
  int fd = t->next_free_fd++;
  
  while(files_lookup(t->next_free_fd) != NULL)
    t->next_free_fd++;
  return fd;
}

/* 
 * Adds file to opened_files_table
 * and returns it's descriptor. 
 */
int files_get_descriptor (void *file, bool is_dir) {
  struct opened_file *f = malloc(sizeof(struct opened_file));
  if (f == NULL) return -1;
  f->descriptor = get_fd();
  f->file = file;
  f->is_dir = is_dir;
  hash_insert(&thread_current()->opened_files_table, &f->elem);
  return f->descriptor;
}

/* 
 * Removes file from opened_files_table,
 * closes it and frees space. 
 */
void files_remove (int fd) {
  struct opened_file *f = files_lookup(fd);
  if (f == NULL) return;

  if (f->is_dir) {
    //lock_acquire(&filesys_lock);
    dir_close((struct dir*)f->file);
    //lock_release(&filesys_lock);
  } else {
    //lock_acquire(&filesys_lock);
    file_close(f->file);
    //lock_release(&filesys_lock);
  }

  struct thread *t = thread_current ();
  hash_delete(&t->opened_files_table, &f->elem);

  t->next_free_fd = f->descriptor < t->next_free_fd ? f->descriptor : t->next_free_fd;
  free(f);
}

/* Returns a hash value for opened_file f. */
unsigned opened_file_hash (const struct hash_elem *f_, void *aux UNUSED) {
  const struct opened_file *f = hash_entry (f_, struct opened_file, elem);
  return hash_bytes (&f->descriptor, sizeof(f->descriptor));
}

/* Returns true if opened_file a precedes opened_file b. */
bool opened_file_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  const struct opened_file *a = hash_entry (a_, struct opened_file, elem);
  const struct opened_file *b = hash_entry (b_, struct opened_file, elem);

  return a->descriptor < b->descriptor;  
}

/* Returns file indexed as fd. */
struct opened_file *
files_lookup (int fd)
{
  struct hash *opened_files = &thread_current()->opened_files_table;
  struct opened_file f;
  struct hash_elem *e;

  f.descriptor = fd;
  e = hash_find (opened_files, &f.elem);
  return e != NULL ? hash_entry (e, struct opened_file, elem) : NULL;
}

void files_open_file_table_dest (struct hash_elem *elem, void *aux UNUSED) {
  struct opened_file *f = hash_entry (elem, struct opened_file, elem);
  if (f->is_dir) {
    //lock_acquire(&filesys_lock);
    dir_close((struct dir*)f->file);
    //lock_release(&filesys_lock);
  } else {
    //lock_acquire(&filesys_lock);
    file_close(f->file);
    //lock_release(&filesys_lock);
  }
  free(f);
}

bool
files_is_directory (int fd) {
  return files_lookup(fd)->is_dir;
}

int files_get_inumber (int fd) {
  struct opened_file *f = files_lookup(fd);
  return f->is_dir ? dir_get_inumber((struct dir*)f->file) : file_get_inumber((struct file*)f->file);
}
