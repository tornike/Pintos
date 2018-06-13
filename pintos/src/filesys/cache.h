#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"

#define CACHE_CAPACITY 64   /* Cache size in blocks */

void cache_init (void);
void cache_destroy (void);

void cache_read (block_sector_t, int, off_t, size_t, void*);
void cache_write (block_sector_t, int, off_t, size_t, const void*);

#endif