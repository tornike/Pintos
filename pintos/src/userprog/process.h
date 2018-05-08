#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H
#define MAX_ARGUMENTS 64

#include "threads/thread.h"


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct process_argument_data 
{
    char file_name[512];
    char cmd_line[512];

    bool load_success;
    struct semaphore load_signal;
    struct thread* parent;
};

#endif /* userprog/process.h */
