#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"

#include "threads/synch.h"

#include <string.h>
#include <bitmap.h>
#include <stdint.h>
#include <hash.h>

static void cache_flush_slot (size_t slot_idx);
static void cache_flush (void);


struct block_slot {
  block_sector_t sector;              /* Sector index in block device. */
  uint8_t data[BLOCK_SECTOR_SIZE];
  bool accessed;
  bool dirty;
  uint32_t user_count;

  //struct lock lock;
};

struct block_slot *buffer_cache;
struct bitmap *free_slots;

struct lock cache_lock;


void cache_init (void) {
  buffer_cache = malloc(sizeof(struct block_slot) * CACHE_CAPACITY);
  int i;
  for (i = 0; i < CACHE_CAPACITY; i++) {
    buffer_cache[i].sector = -1;  /* At the beginning there must be no cache hits. */
    buffer_cache[i].accessed = false;
    buffer_cache[i].dirty = false;
    buffer_cache[i].user_count = 0;
    //lock_init(&buffer_cache[i].lock);
  }
  
  free_slots = bitmap_create(CACHE_CAPACITY);
  lock_init(&cache_lock);
}

void cache_destroy (void) {
  cache_flush ();
  free(buffer_cache);
  bitmap_destroy(free_slots);
}


/* Helpers */

static int cache_find (block_sector_t sector) {
  int i = 0;
  for (; i < CACHE_CAPACITY; i++)
    if (buffer_cache[i].sector == sector)
      return i;
  return -1;
}

static void cache_flush_slot (size_t slot_idx) {
  block_write (fs_device, buffer_cache[slot_idx].sector, buffer_cache[slot_idx].data);
  buffer_cache[slot_idx].dirty = false;
}

static void cache_flush (void) {
  lock_acquire(&cache_lock);
  int i = 0;
  for (; i < CACHE_CAPACITY; i++) {
    //lock_acquire (&buffer_cache[i].lock);
    if (buffer_cache[i].dirty)
      cache_flush_slot (i);
    if (buffer_cache[i].accessed)
      buffer_cache[i].accessed = false;
    if (buffer_cache[i].user_count == 0)
      bitmap_reset (free_slots, i);
    //lock_release (&buffer_cache[i].lock);
  }
  lock_release(&cache_lock);
}

static int cache_get_slot (void) {
  size_t i = bitmap_scan_and_flip(free_slots, 0, 1, false);
  if (i != BITMAP_ERROR) return i;
  while (true) {
    i = 0;
    for (; i < CACHE_CAPACITY; i++) {
      if (buffer_cache[i].accessed)
        buffer_cache[i].accessed = false;
      else if (buffer_cache[i].user_count == 0) { /* Evict */
        if (buffer_cache[i].dirty)  /* Flush */
          cache_flush_slot (i);
        return i;
      }
    }
  }
}

static int cache_load (block_sector_t sector) {
  int slot_idx = cache_get_slot ();
  buffer_cache[slot_idx].sector = sector;
  block_read (fs_device, sector, buffer_cache[slot_idx].data);
  return slot_idx;
}

void cache_read (block_sector_t sector, int sector_ofs, off_t buffer_ofs, size_t size, void *buffer_) {
  uint8_t *buffer = buffer_;

  lock_acquire(&cache_lock);
  int slot_idx = cache_find (sector);
  if (slot_idx == -1) /* Cache miss */
    slot_idx = cache_load (sector);
  buffer_cache[slot_idx].user_count++;
  lock_release(&cache_lock);

  //lock_acquire (&buffer_cache[slot_idx].lock);
  memcpy (buffer + buffer_ofs, buffer_cache[slot_idx].data + sector_ofs, size);
  buffer_cache[slot_idx].accessed = true;
  buffer_cache[slot_idx].user_count--;
  //lock_release (&buffer_cache[slot_idx].lock);
}


void cache_write (block_sector_t sector, int sector_ofs, off_t buffer_ofs, size_t size, const void *buffer_) {
  const uint8_t *buffer = buffer_;

  lock_acquire(&cache_lock);
  int slot_idx = cache_find (sector);
  if (slot_idx == -1) /* Cache miss */
    slot_idx = cache_load (sector);
  buffer_cache[slot_idx].user_count++;
  lock_release(&cache_lock);

  //lock_acquire (&buffer_cache[slot_idx].lock);
  memcpy (buffer_cache[slot_idx].data + sector_ofs, buffer + buffer_ofs, size);
  buffer_cache[slot_idx].accessed = true;
  buffer_cache[slot_idx].dirty = true;
  buffer_cache[slot_idx].user_count--;
  //lock_release (&buffer_cache[slot_idx].lock);
}
