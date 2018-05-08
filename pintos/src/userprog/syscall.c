#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "userprog/process.h"


#define PIECE_SIZE 128
static void syscall_handler (struct intr_frame *);

static int write (int, const void*, unsigned);
static int read (int, void*, unsigned);
static int filesize (int);
static void seek (int, unsigned);
static unsigned tell (int);
static void exec (struct intr_frame *f);
static int open(const char*);
static void close(int);
static bool create (const char*, unsigned);
static bool remove (const char*);
static void wait (struct intr_frame *f);
static void exit(int);


static bool 
is_valid_ptr (void* ptr, size_t size) 
{
	return ptr != NULL && is_user_vaddr((char*)ptr + size);
}

static bool
is_valid_string (void* ptr) 
{ 
  size_t i;
  for (i = 0;; i++)
  {
    if (!is_valid_ptr(ptr, 1)) return false;
    ptr++;
    if (*(char*)ptr == '\0') return true;
  }
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{

  if (!is_valid_ptr(f->esp, sizeof(uint32_t))) exit(-1);
  uint32_t* args = ((uint32_t*) f->esp);
  //printf("System call number: %d\n", args[0]);
  
  if (args[0] == SYS_WRITE) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(int) + sizeof(void *) + sizeof(unsigned))) exit(-1);
    int fd = args[1];
    const void* buffer = (const void*)args[2];
    unsigned size = args[3];
    f->eax = write(fd, buffer, size);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_READ) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(int) + sizeof(void *) + sizeof(unsigned))) exit(-1);
    int fd = args[1];
    void* buffer = (void*)args[2];
    unsigned size = args[3];
    f->eax = read(fd, buffer, size);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_FILESIZE) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(int))) exit(-1);
    int fd = args[1];
    f->eax = filesize(fd);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_SEEK) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(int) + sizeof(unsigned))) exit(-1);
    int fd = args[1];
    unsigned position = args[2];
    seek(fd, position);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_TELL) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(int))) exit(-1);
    int fd = args[1];   
    f->eax = tell(fd);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_EXEC) {
    exec(f);
  } else if (args[0] == SYS_WAIT) {
    wait(f);
  } else if (args[0] == SYS_OPEN) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(char *))) exit(-1);
    f->eax = open((const char *)args[1]);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_CLOSE) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(int))) exit(-1);    
    close(args[1]);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_CREATE) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(char*) + sizeof(unsigned))) exit(-1);
    const char* file_name = (const char*)args[1];
    unsigned initial_size = args[2];
    f->eax = (uint32_t)create(file_name, initial_size);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_REMOVE) {
    lock_acquire(&filesys_lock);
    if (!is_valid_ptr(f->esp, sizeof(char*))) exit(-1);
    const char* file_name = (const char*)args[1];
    f->eax = (uint32_t)remove(file_name);
    lock_release(&filesys_lock);
  } else if (args[0] == SYS_EXIT) {
    int status = args[1];
    if (!is_valid_ptr(f->esp, sizeof(int))) exit(-1);
    exit(status);
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
  } else if (args[0] == SYS_PRACTICE) {
    if (!is_valid_ptr(f->esp, sizeof(int))) exit(-1);
    f->eax = args[1] + 1;
  }
}

static void
exec (struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  if (!is_valid_ptr(args, sizeof(uint32_t) + sizeof(char*))) exit(-1);

  const char* file = args[1];
  if (!is_valid_string(file)) exit(-1);
  f->eax = process_execute(file);
}

static void
wait (struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  if (!is_valid_ptr(args, sizeof(uint32_t) + sizeof(int))) exit(-1);

  int child_pid = args[1];
  f->eax = process_wait(child_pid);
}

static int 
write (int fd, const void *buffer, unsigned size) 
{
  if (!is_valid_ptr((void*)buffer, size) || fd < 0 || fd >= MAX_FILE_COUNT) exit(-1);

  if (fd == STDOUT_FILENO) {
    const char* ptr = buffer;
    unsigned size_copy = size;
    while (size >= PIECE_SIZE) {
      putbuf(ptr, PIECE_SIZE);
      size -= PIECE_SIZE;
      ptr += PIECE_SIZE;
    }
    putbuf(ptr, size);
    return size_copy;
  } else {
    struct file* file = thread_current()->file_descriptors[fd];
    if (file == NULL) exit(-1);
    return file_write(file, buffer, size);
  }
}

static int 
read (int fd, void* buffer, unsigned size) 
{
  if (!is_valid_ptr(buffer, size)) exit(-1);

  if (fd < 0 || fd >= MAX_FILE_COUNT) return -1;

  if (fd == STDIN_FILENO) {
    unsigned i;
    for(i = 0; i < size; i++)
      ((uint8_t*)buffer)[i] = input_getc();
    return size;
  } else {
    struct file* file = thread_current()->file_descriptors[fd];
    if (file == NULL) return -1;
    return file_read(file, buffer, size);
  }
}

static void
seek (int fd, unsigned position) 
{
  if (fd < 0 || fd >= MAX_FILE_COUNT) exit(-1);

  struct file* file = thread_current()->file_descriptors[fd];
  if (file == NULL) exit(-1);
  file_seek(file, position);
}

static unsigned 
tell (int fd) 
{
  if (fd < 0 || fd >= MAX_FILE_COUNT) exit(-1);

  struct file* file = thread_current()->file_descriptors[fd];
  if (file == NULL) exit(-1);
  return file_tell(file);
}

static int 
filesize (int fd) 
{
  if (fd < 0 || fd >= MAX_FILE_COUNT) exit(-1);

  struct file* file = thread_current()->file_descriptors[fd];
  if (file == NULL) exit(-1);
  return file_length(file);
}

static int 
open (const char *file_name) 
{
  if (!is_valid_string((char*)file_name)) exit(-1);

  struct file* file = filesys_open(file_name);
  if (file == NULL) return -1;
  struct thread* curr = thread_current();
  int fd = curr->next_free_fd;
  curr->file_descriptors[fd] = file;
  while(curr->next_free_fd != MAX_FILE_COUNT - 1) {
    curr->next_free_fd++;
    if (curr->file_descriptors[curr->next_free_fd] == NULL) break;
  }
  return fd;
}

static void 
close (int fd) 
{
  if (fd < 0 || fd >= MAX_FILE_COUNT) return;

  struct thread* curr = thread_current();
  struct file* file = curr->file_descriptors[fd];
  if (file == NULL) return;

  file_close(file);
  curr->file_descriptors[fd] = NULL;
  curr->next_free_fd = fd;
}

static bool 
create (const char *file, unsigned initial_size) 
{
  if (!is_valid_string((char*)file)) exit(-1);
  return filesys_create(file, initial_size);
}

static bool 
remove (const char *file) 
{
  if (!is_valid_string((char*)file)) exit(-1);
  return filesys_remove(file);
}

static void 
exit (int status)
{
  struct thread* curr = thread_current ();
  printf("%s: exit(%d)\n", (char*)curr->name, status);
  curr->exit_status = status;
  thread_exit();
}

