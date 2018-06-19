#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;


static void do_format (void);
static int get_next_part (char part[NAME_MAX + 1], const char **srcp);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  cache_destroy ();
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
    const char *src = *srcp;
    char *dst = part;

    /* Skip leading slashes. If it’s all slashes, we’re done. */
    while (*src == '/')
        src++;
    if (*src == '\0' || *src == ' ')
        return 0;

    /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
    while (*src != '/' && *src != '\0' && *src != ' ') {
        if (dst < part + NAME_MAX)
            *dst++ = *src;
        else
            return -1;
        src++;
    }
    *dst = '\0';

    /* Advance source pointer. */
    *srcp = src;
    return 1;
}

/* -1: Invalid Path
 *  0: Not Found, but it is last part
 *  1: Found, and it is last part
 */
static int
find_file (const char *src, char* filename, struct dir **cwd, struct inode **next_inode)
{
  int result = -1;
  *cwd = NULL;
  struct inode* cwd_inode = thread_current()->cwd_inode;
  /* Check if given name corresponds to relative
      or absolute path. */
  if (src[0] == '/') {
    *cwd = dir_open_root();
    if (src[1] == '\0' || src[1] == ' ') { /* Searched file is / */
      *next_inode = inode_open (ROOT_DIR_SECTOR);
      return 1;
    }
  } else {
    if (cwd_inode == NULL) return result;
    *cwd = dir_open (inode_reopen(cwd_inode));
  }

  filename[0] = '\0';
  
  /* Iterate over directories until we hit a file or the end. */
  while (get_next_part (filename, &src) > 0) 
  { 
    bool is_last_part = *src == '\0' || *src == ' ';
    bool found = dir_lookup (*cwd, filename, next_inode);

    if (found) {
      if (is_last_part) {
        result = 1;
        break;
      }
    } else if (is_last_part) {
      result = 0;
      break;
    } else /* Invalid Path */
      break;
    
    dir_close(*cwd);
    *cwd = dir_open (*next_inode);
  }
  return result;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{ 
  char filename[NAME_MAX + 1];
  struct dir *parent_dir;
  struct inode *inode;
  bool res = find_file(name, filename, &parent_dir, &inode);
  if (res != 0) {
    dir_close(parent_dir);
    return false;
  }
  
  block_sector_t inode_sector = 0;
  if (parent_dir == NULL || !free_map_allocate (1, &inode_sector))
    return false;
  if (!inode_create (inode_sector, initial_size, is_dir)) {
    free_map_release (inode_sector, 1);
    return false;
  }
  bool success = dir_add (parent_dir, filename, inode_sector);;
  if (success && is_dir) {
    struct dir* new_dir = dir_open(inode_open (inode_sector));
    success &= (dir_add (new_dir, ".", inode_sector)
                    && dir_add (new_dir, "..", inode_get_inumber(dir_get_inode(parent_dir))));
    dir_close (new_dir);
  }
  dir_close (parent_dir);
  if (!success)
    free_map_release (inode_sector, 1);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
void *
filesys_open (const char *name, bool *is_dir)
{
  struct dir *dir;
  struct inode *inode;

  char filename[NAME_MAX + 1];
  int res = find_file (name, filename, &dir, &inode);
  dir_close (dir);
  if (res != 1) {
    inode_close(inode);
    return NULL;
  }
  
  bool tmp = inode_is_dir(inode);
  if (is_dir != NULL) *is_dir = tmp;
  void* result = tmp ? (void*)dir_open (inode) : (void*)file_open (inode);
  return result;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *parent_dir;
  struct inode *inode;

  char filename[NAME_MAX + 1];
  int res = find_file (name, filename, &parent_dir, &inode);
  bool success = false;
  if (res == 1) {
    if (inode_is_dir(inode)) {
      struct dir* target = dir_open(inode);
      char tmp[NAME_MAX + 1];
      bool can_be_removed = !dir_readdir(target, tmp);
      dir_close(target);
      if (can_be_removed) { // The directory is empty.
        block_sector_t sector = inode_get_inumber(inode);
        struct thread* curr = thread_current();
        if (sector == inode_get_inumber(curr->cwd_inode)) {
          inode_close(curr->cwd_inode);
          curr->cwd_inode = NULL;
        }
        success = dir_remove (parent_dir, filename);
      }
    } else { // Just File.
      success = dir_remove (parent_dir, filename);
    }
  }
  dir_close (parent_dir);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

bool
filesys_change_dir (const char* path) {
  struct dir *dir;
  struct inode *inode;
  char filename[NAME_MAX + 1];
  int res = find_file (path, filename, &dir, &inode);
  dir_close(dir);
  bool success = false;
  if (res == 1 && inode_is_dir(inode)) {
    struct thread* curr = thread_current();
    if (curr->cwd_inode != NULL) inode_close(curr->cwd_inode);
    curr->cwd_inode = inode;
    success = true;
  }
  return success;
}

int
filesys_get_inode_number (const void *file) {
  return inode_get_inumber((const struct inode*)file);
}