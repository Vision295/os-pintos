#include "threads/synch.h"


struct swap_table { 
    struct bitmap *used_slots; 
    struct block *swap_block;
    size_t total_slots; 
    struct lock lock; 
};

void swap_init(void);
size_t swap_out(void *kpage);
void swap_in(void *kpage, size_t slot);



