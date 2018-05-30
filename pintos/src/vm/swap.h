#ifndef VM_SWAP_H
#define VM_SWAP_H

/* Index of a slot in swap. */
typedef long swap_slot_t;

void swap_init(void);
swap_slot_t swap_out(void*);
void swap_in(swap_slot_t, void*);

#endif
