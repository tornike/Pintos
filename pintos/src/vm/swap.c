#include "vm/swap.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"


struct block* swap_block_device;
struct bitmap* swap_slots;
swap_slot_t next_free_slot;
size_t bmap_size;

#define BLOCKS_IN_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)


void swap_init(void) {
  swap_block_device = block_get_role(BLOCK_SWAP);
  bmap_size = block_size(swap_block_device) / BLOCKS_IN_PAGE;
  swap_slots = bitmap_create(bmap_size);
}

swap_slot_t swap_out(void *p_addr) {
  swap_slot_t slot = bitmap_scan_and_flip (swap_slots, 0, 1, false);
  if ((size_t)slot == BITMAP_ERROR) PANIC("Kernel Bug: Swap Full");
  uint8_t *block_chunk = p_addr;

  /* Write data to swap */
  block_sector_t block_idx = slot * BLOCKS_IN_PAGE;
  block_sector_t end_block_idx = block_idx + BLOCKS_IN_PAGE;
  for(; block_idx < end_block_idx; block_idx++) {
    block_write(swap_block_device, block_idx, block_chunk);
    block_chunk += BLOCK_SECTOR_SIZE;
  }

  return slot;
}

void swap_in(swap_slot_t slot, void *p_addr) {
  uint8_t *block_chunk = p_addr;

  /* Write data to page */
  block_sector_t block_idx = slot * BLOCKS_IN_PAGE;
  block_sector_t end_block_idx = block_idx + BLOCKS_IN_PAGE;
  for(; block_idx < end_block_idx; block_idx++) {
    block_read(swap_block_device, block_idx, block_chunk);
    block_chunk += BLOCK_SECTOR_SIZE;
  }

  bitmap_flip(swap_slots, slot);
}

