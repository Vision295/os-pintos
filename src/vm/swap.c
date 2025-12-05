#include "threads/synch.h"
#include <stdio.h>
#include <stdlib.h>
#include "devices/block.h"
#include "swap.h"
#include "threads/vaddr.h"
#include "bitmap.h"
#include <string.h>

int get_sectors_per_page() {
    return PGSIZE / BLOCK_SECTOR_SIZE;
}

void swap_init(void)  {
    printf("[swap_init] - Initializing swap space\n");

    swap_table = malloc(sizeof(struct swap_table));
    if (swap_table == NULL) 
        PANIC("Failed to allocate swap table");

    swap_table->swap_block = block_get_role(BLOCK_SWAP);
    ASSERT(swap_table->swap_block != NULL);

    // Each page requires PAGE_SIZE / BLOCK_SECTOR_SIZE sectors
    int sectors_per_page = get_sectors_per_page();
    swap_table->total_slots = block_size(swap_table->swap_block) / sectors_per_page;

    // Create bitmap marking all slots free
    swap_table->used_slots = bitmap_create(swap_table->total_slots);
    bitmap_set_all(swap_table->used_slots, false);
    lock_init(&swap_table->lock);
}

size_t swap_out(void *kpage) {
    // check page alignement
    ASSERT(pg_ofs(kpage) == 0);
    
    printf("[swap_out] - Swapping out kpage=%p\n", kpage);

    lock_acquire(&swap_table->lock);

    // Find free slot in bitmap
    int slot = bitmap_scan_and_flip(swap_table->used_slots,
                                0, 1, false);

    // if slot already in use
    ASSERT(bitmap_test(swap_table->used_slots, slot));
    if (slot == BITMAP_ERROR) 
        PANIC("Swap full!");

    // Write page into multiple sectors
    size_t base_sector = slot * get_sectors_per_page();

    for (int i = 0; i < get_sectors_per_page(); i++)
        block_write(swap_table->swap_block,
                    base_sector + i,
                    kpage + i * BLOCK_SECTOR_SIZE);

    // clear frame after writing
    memset(kpage, 0, PGSIZE);
    lock_release(&swap_table->lock);
    return slot;
}

void swap_in(void *kpage, size_t slot) {
    ASSERT(slot < swap_table->total_slots);

    printf("[swap_in] - Swapping in kpage=%p from slot=%zu\n", kpage, slot);
    lock_acquire(&swap_table->lock);

    size_t base_sector = slot * get_sectors_per_page();

    // Read back into newly allocated frame
    for (int i = 0; i < get_sectors_per_page(); i++)
        block_read(swap_table->swap_block,
                   base_sector + i,
                   kpage + i * BLOCK_SECTOR_SIZE);

    // Mark slot free again
    bitmap_set(swap_table->used_slots, slot, false);
    lock_release(&swap_table->lock);
}