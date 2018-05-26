#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

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
static void create_handler (struct intr_frame *f);
static void remove_handler (struct intr_frame *f);
static void exit_handler (struct intr_frame *f);

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
  else if (syscall_num == SYS_REMOVE) remove_handler(f);
  else if (syscall_num == SYS_EXIT) exit_handler(f);
  else if (syscall_num == SYS_HALT) shutdown_power_off();
  else if (syscall_num == SYS_PRACTICE) {
    if (!is_valid_ptr(f->esp, sizeof(uint32_t) + sizeof(int))) exit_helper(-1);
    f->eax = args[1] + 1;
  }
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
    struct file* file = thread_current()->file_descriptors[fd];
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
    struct file* file = thread_current()->file_descriptors[fd];
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

  struct file* file = thread_current()->file_descriptors[fd];
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

  struct file* file = thread_current()->file_descriptors[fd];
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

  struct file* file = thread_current()->file_descriptors[fd];
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
  struct thread* curr = thread_current();
  int fd = curr->next_free_fd;
  curr->file_descriptors[fd] = file;
  while(curr->next_free_fd != MAX_FILE_COUNT - 1) {
    curr->next_free_fd++;
    if (curr->file_descriptors[curr->next_free_fd] == NULL) break;
  }
  f->eax = fd;
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

  struct thread* curr = thread_current();
  struct file* file = curr->file_descriptors[fd];
  if (file == NULL) return;

  lock_acquire(&filesys_lock);
  file_close(file);
  lock_release(&filesys_lock);

  curr->file_descriptors[fd] = NULL;
  curr->next_free_fd = fd;
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
  f->eax = filesys_create(file_name, initial_size);
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

static mapid_t
mmap_handler (int fd, void *addr) 
{
  if (fd == 0 || fd == 1) return -1;

  lock_acquire(&filesys_lock);  
  struct file *file = file_reopen(thread_current()->file_descriptors[fd]);
  size_t file_size = file_length(file);
  lock_release(&filesys_lock);  

  if (file == NULL || file_size <= 0) return MAP_FAILED;
  if (addr == NULL || pg_ofs(addr) != 0) return MAP_FAILED;

  void *temp = addr;
  while (temp < (temp + file_size)) {
    if (pagedir_get_page(thread_current()->pagedir, temp) != NULL) return MAP_FAILED;
    temp = (char *)temp + PGSIZE;
  }

  off_t offset = 0;
  while (offset < file_size) {
    size_t read_bytes = file_size < PGSIZE ? file_size : PGSIZE;

    /* Get virtual page of memory. */
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL)
      return MAP_FAILED;

    /* Setup page with correct values. */
    struct file_info *file_info = NULL;
    file_info = malloc(sizeof(struct file_info));
    file_info->file = file;
    file_info->offset = offset;
    file_info->length = read_bytes;

    hash_insert(&thread_current()->sup_page_table, &page->elem);    

    page->v_addr = addr;
    page->frame = NULL;
    page->writable = true;
    page->file_info = file_info;

    offset += read_bytes;
    addr += PGSIZE;
  }
  return 1337; // !!
}
