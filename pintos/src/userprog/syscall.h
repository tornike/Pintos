#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"
#include <hash.h>

void syscall_init (void);

struct map {
    void *addr;
    mapid_t mapping;
    int fd;

    struct hash_elem elem;
};

struct lock filesys_lock;

#endif /* userprog/syscall.h */
