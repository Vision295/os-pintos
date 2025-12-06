#ifndef SWAP_H
#define SWAP_H

#include "threads/synch.h"

struct swap_table { 
    struct bitmap *used_slots; 
    struct block *swap_block;
    size_t total_slots; 
    struct lock lock; 
};

extern struct swap_table* swap_table;

void swap_init(void);
size_t swap_out(void *kpage);
void swap_in(void *kpage, size_t slot);
int get_sectors_per_page(void);
void swap_free(size_t slot);


#endif /* vm/swap.h */