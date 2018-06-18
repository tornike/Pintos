#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/files.h"
#include "vm/page.h"
#include "vm/mmap.h"

#define PIECE_SIZE 128  /* Size of chunk written on console. */

static void syscall_handler (struct intr_frame *);

static void write_handler (struct intr_frame *f);
static void read_handler (struct intr_frame *f);
static void filesize_handler (struct intr_frame *f);
static void seek_handler (struct intr_frame *f);
static void tell_handler (struct intr_frame *f);
static void exec_handler (struct intr_frame *f);
static void wait_handler (struct intr_frame *f);
static void open_handler (struct intr_frame *f);
static void close_handler (struct intr_frame *f);
static void mmap_handler (struct intr_frame *f);
static void munmap_handler (struct intr_frame *f);
static void create_handler (struct intr_frame *f);
static void remove_handler (struct intr_frame *f);
static void exit_handler (struct intr_frame *f);
static void readdir_handler (struct intr_frame *f);
static void mkdir_handler (struct intr_frame *f);
static void chdir_handler (struct intr_frame *f);
static void isdir_handler (struct intr_frame *f);
static void inumber_handler (struct intr_frame *f);

static void exit_helper(int status);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static bool 
is_valid_ptr (const void* ptr, size_t size) 
{
	return ptr != NULL && is_user_vaddr((char *)ptr + size);
}

static bool
is_valid_string (const void* ptr) 
{ 
  size_t i;
  for (i = 0;; i++)
  {
    if (!is_valid_ptr(ptr, 1)) return false;
    ptr++;
    if (*(char *)ptr == '\0') return true;
  }
}

static void
syscall_handler (struct intr_frame *f)
{
  
  if (!is_valid_ptr(f->esp, sizeof(uint32_t))) exit_helper(-1);
  uint32_t* args = ((uint32_t*) f->esp);
  uint32_t syscall_num = args[0];
  
  if (syscall_num == SYS_WRITE) write_handler(f);
  else if (syscall_num == SYS_READ) read_handler(f);
  else if (syscall_num == SYS_EXEC) exec_handler(f);
  else if (syscall_num == SYS_WAIT) wait_handler(f);
  else if (syscall_num == SYS_FILESIZE) filesize_handler(f);
  else if (syscall_num == SYS_SEEK) seek_handler(f);
  else if (syscall_num == SYS_TELL) tell_handler(f);
  else if (syscall_num == SYS_OPEN) open_handler(f);
  else if (syscall_num == SYS_CLOSE) close_handler(f);
  else if (syscall_num == SYS_CREATE) create_handler(f);
  else if (syscall_num == SYS_MMAP) mmap_handler(f);
  else if (syscall_num == SYS_MUNMAP) munmap_handler(f);
  else if (syscall_num == SYS_REMOVE) remove_handler(f);
  else if (syscall_num == SYS_MKDIR) mkdir_handler(f);
  else if (syscall_num == SYS_CHDIR) chdir_handler(f);
  else if (syscall_num == SYS_READDIR) readdir_handler(f);
  else if (syscall_num == SYS_EXIT) exit_handler(f);
  else if (syscall_num == SYS_ISDIR) isdir_handler(f);
  else if (syscall_num == SYS_INUMBER) inumber_handler(f);
  else if (syscall_num == SYS_HALT) shutdown_power_off();
  else if (syscall_num == SYS_PRACTICE) {
    if (!is_valid_ptr(f->esp, sizeof(uint32_t) + sizeof(int))) exit_helper(-1);
    f->eax = args[1] + 1;
  } else exit_helper(-1);
}

static void
exec_handler (struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(char*);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  const char* file_name = (const char*)args[1];

  if (!is_valid_string(file_name)) exit_helper(-1);
  f->eax = process_execute(file_name);
}

static void
wait_handler (struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int child_pid = args[1];

  f->eax = process_wait(child_pid);
}

static void 
write_handler (struct intr_frame *f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int) + sizeof(void*) + sizeof(unsigned);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];
  const void* buffer = (void*)args[2];
  unsigned size = args[3];

  if (!is_valid_ptr(buffer, size) || fd < 0 || fd >= MAX_FILE_COUNT) exit_helper(-1);

  if (fd == STDOUT_FILENO) {
    char* ptr = (char*)buffer;
    unsigned size_copy = size;
    while (size >= PIECE_SIZE) {
      putbuf(ptr, PIECE_SIZE);
      size -= PIECE_SIZE;
      ptr += PIECE_SIZE;
    }
    putbuf(ptr, size);
    f->eax = size_copy;
  } else {
    struct file* file = files_lookup(fd)->file;
    if (file == NULL) exit_helper(-1);
    lock_acquire(&filesys_lock);
    f->eax = file_write(file, buffer, size);
    lock_release(&filesys_lock);
  }
}

static void 
read_handler (struct intr_frame *f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int) + sizeof(void*) + sizeof(unsigned);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];
  void* buffer = (void*)args[2];
  unsigned size = args[3];

  if (fd < 0 || fd >= MAX_FILE_COUNT) {
    f->eax = -1;
    return;
  }
  if (!is_valid_ptr(buffer, size)) exit_helper(-1);

  if (fd == STDIN_FILENO) {
    unsigned i;
    for(i = 0; i < size; i++)
      ((uint8_t*)buffer)[i] = input_getc();
    f->eax = size;
  } else {
    struct file* file = files_lookup(fd)->file;
    if (file == NULL) f->eax = -1;
    else {
      lock_acquire(&filesys_lock);
      f->eax = file_read(file, buffer, size);
      lock_release(&filesys_lock);
    }
  }
}

static void
seek_handler (struct intr_frame* f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int) + sizeof(unsigned);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];
  unsigned position = args[2];

  if (fd < 0 || fd >= MAX_FILE_COUNT) exit_helper(-1);

  struct file* file = files_lookup(fd)->file;
  if (file == NULL) exit_helper(-1);

  lock_acquire(&filesys_lock);
  file_seek(file, position);
  lock_release(&filesys_lock);
}

static void 
tell_handler (struct intr_frame* f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];

  if (fd < 0 || fd >= MAX_FILE_COUNT) exit_helper(-1);

  struct file* file = files_lookup(fd)->file;
  if (file == NULL) exit_helper(-1);

  lock_acquire(&filesys_lock);
  f->eax = file_tell(file);
  lock_release(&filesys_lock);
}

static void 
filesize_handler (struct intr_frame* f) 
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];

  if (fd < 0 || fd >= MAX_FILE_COUNT) exit_helper(-1);

  struct file* file = files_lookup(fd)->file;
  if (file == NULL) exit_helper(-1);

  lock_acquire(&filesys_lock);
  f->eax = file_length(file);
  lock_release(&filesys_lock);
}

static void 
open_handler (struct intr_frame* f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(char*);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  const char* file_name = (const char*)args[1];

  if (!is_valid_string(file_name)) exit_helper(-1);

  lock_acquire(&filesys_lock);
  struct file* file = filesys_open(file_name);
  lock_release(&filesys_lock);

  if (file == NULL) {
    f->eax = -1;
    return;
  }

  f->eax = files_get_descriptor(file);
}

static void 
close_handler (struct intr_frame* f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];

  if (fd < 0 || fd >= MAX_FILE_COUNT) return;

  files_remove(fd);
}

static void 
create_handler (struct intr_frame* f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(char*) + sizeof(unsigned);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);
  
  /* Arguments */
  const char* file_name = (const char*)args[1];
  unsigned initial_size = args[2];

  if (!is_valid_string(file_name)) exit_helper(-1);
  lock_acquire(&filesys_lock);
  f->eax = filesys_create(file_name, initial_size, false);
  lock_release(&filesys_lock);
}

static void 
remove_handler (struct intr_frame* f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(char*);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  const char* file_name = (const char*)args[1];

  if (!is_valid_string(file_name)) exit_helper(-1);
  lock_acquire(&filesys_lock);
  f->eax = filesys_remove(file_name);
  lock_release(&filesys_lock);
}

static void
exit_handler (struct intr_frame *f) 
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int status = args[1];
  exit_helper (status);
}

static void 
exit_helper (int status)
{
  struct thread* curr = thread_current ();
  curr->exit_status = status;
  printf("%s: exit(%d)\n", (char*)curr->name, status);
  thread_exit();
}

static void
mmap_handler (struct intr_frame *f) // memory leak.
{
  uint32_t *args = ((uint32_t *) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int) + sizeof(void *);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];
  void *addr = (void*)args[2];

  if (fd <= 1 || fd >= MAX_FILE_COUNT || addr == NULL) {
    f->eax = MAP_FAILED;
    return;
  }
  struct thread *curr = thread_current();

  lock_acquire(&filesys_lock);  
  struct file *file = file_reopen(files_lookup(fd)->file);
  size_t file_size = file_length(file);
  lock_release(&filesys_lock);

  if (!is_valid_ptr(addr, file_size)) exit_helper(-1);

  if (file == NULL || file_size <= 0 || pg_ofs(addr) != 0) {
    f->eax = MAP_FAILED;
    return;
  }

  struct mmap *m = mmap_allocate(file, addr);
  off_t offset = 0;
  size_t read_bytes = file_size;
  while (read_bytes != 0) {
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;

    /* Check that address is not mapped */
    if (page_lookup(&curr->sup_page_table, m->end_addr) != NULL) {
      mmap_deallocate(m);
      f->eax = MAP_FAILED;
      return;
    }

    /* Setup file info */
    struct file_info *file_info = malloc(sizeof(struct file_info));
    if (file_info == NULL) {
      mmap_deallocate(m);
      f->eax = MAP_FAILED;
      return;
    }
    file_info->file = file;
    file_info->offset = offset;
    file_info->length = page_read_bytes;
    file_info->mapped = true;

    /* Get virtual page of memory */
    page_allocate(m->end_addr, true, file_info);

    read_bytes -= page_read_bytes;
    offset += page_read_bytes;
    m->end_addr += PGSIZE;
  }
    
  hash_insert(&curr->mapping_table, &m->elem);

  f->eax = m->mapping;
}

static void munmap_handler (struct intr_frame *f)
{
  uint32_t *args = ((uint32_t *) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(mapid_t);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  mapid_t mapping = args[1];

  struct thread *curr = thread_current();

  struct mmap *m = mmap_lookup(&curr->mapping_table, mapping);
  if (m == NULL) return;

  mmap_remove(m);
  mmap_deallocate(m);
}

static void mkdir_handler (struct intr_frame *f)
{
  uint32_t *args = ((uint32_t *) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(char*);
  if (!is_valid_ptr (args, arguments_size)) exit_helper (-1);

  /* Arguments */
  const char *path = (const char *) args[1];
  if (!is_valid_string (path)) exit_helper (-1);

  f->eax = filesys_create (path, 0, true);
}

static void chdir_handler (struct intr_frame *f)
{
  uint32_t *args = ((uint32_t *) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(char*);
  if (!is_valid_ptr (args, arguments_size)) exit_helper (-1);

  /* Arguments */
  const char *path = (const char *) args[1];
  if (!is_valid_string (path)) exit_helper (-1);

  f->eax = filesys_change_dir (path);
}

static void isdir_handler (struct intr_frame *f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];

  if (fd < 0 || fd >= MAX_FILE_COUNT) exit_helper(-1);
  f->eax = files_is_directory(fd);
}

static void inumber_handler (struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];

  if (fd < 0 || fd >= MAX_FILE_COUNT) exit_helper(-1);

  f->eax = files_get_inumber(fd);
}

static void readdir_handler (struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  int arguments_size = sizeof(uint32_t) + sizeof(int) + sizeof(char*);
  if (!is_valid_ptr(args, arguments_size)) exit_helper(-1);

  /* Arguments */
  int fd = args[1];
  char *name = (char*)args[2];
  if (fd < 0 || fd >= MAX_FILE_COUNT) exit_helper(-1);
  if (!is_valid_string (name)) exit_helper (-1);

  if (!files_is_directory(fd)) {
    f->eax = false;
    return;
  }
  struct dir *dir = (struct dir*)files_lookup(fd);
  f->eax = dir_readdir(dir, name);
}