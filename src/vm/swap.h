// swap.h
#ifndef SWAP_H
#define SWAP_H

#include "devices/block.h"
#include "bitmap.h"
#include "threads/synch.h"

void swap_init(void);
size_t swap_out(void *kpage);
void swap_in(void *kpage, size_t slot);
void swap_free(size_t slot);

#endif
