#ifndef USERPROG_FILES_H
#define USERPROG_FILES_H

#include <hash.h>
#include "filesys/file.h"

struct opened_file {
  int descriptor;
  struct file *file;

  struct hash_elem elem;
};

int files_get_descriptor (struct file*);
void files_remove (int);
unsigned opened_file_hash (const struct hash_elem*, void* UNUSED);
bool opened_file_less (const struct hash_elem*, const struct hash_elem*, void* UNUSED);
struct opened_file *files_lookup (int);
bool files_is_directory (int);
bool files_get_inumber (int);
void files_open_file_table_dest (struct hash_elem*, void*);

#endif