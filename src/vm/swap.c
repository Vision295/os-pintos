#include "threads/synch.h"
#include <stdio.h>


void swap_init(void)  {
    printf("[swap_init] - Initializing swap space\n");

    // swap_table = malloc(struct swap_table)

    // swap_table.swap_block = block_get_role(BLOCK_SWAP)
    // ASSERT(swap_table.swap_block != NULL)

    // // Each page requires PAGE_SIZE / BLOCK_SECTOR_SIZE sectors
    // sectors_per_page = PGSIZE / BLOCK_SECTOR_SIZE
    // swap_table.total_slots = block_size(swap_table.swap_block) / sectors_per_page

    // // Create bitmap marking all slots free
    // swap_table.used_slots = bitmap_create(swap_table.total_slots)
    // bitmap_set_all(swap_table.used_slots, false)

    // lock_init(&swap_table.lock)

    // print("Swap initialized: %zu slots\n", swap_table.total_slots)
}

size_t swap_out(void *kpage) {
    printf("[swap_out] - Swapping out kpage=%p\n", kpage);

    // lock_acquire(&swap_table.lock)

    // // Find free slot in bitmap
    // slot = bitmap_scan_and_flip(swap_table.used_slots,
    //                             0, 1, false)

    // if slot == BITMAP_ERROR:
    //     PANIC("Swap full!")

    // print("SWAP_OUT: Writing page at kpage=%p into slot=%zu\n",
    //       kpage, slot)

    // // Write page into multiple sectors
    // base_sector = slot * sectors_per_page

    // for i in range(0, sectors_per_page):
    //     block_write(swap_table.swap_block,
    //                 base_sector + i,
    //                 kpage + i * BLOCK_SECTOR_SIZE)

    // lock_release(&swap_table.lock)

    // return slot
}

void swap_in(void *kpage, size_t slot) {
    printf("[swap_in] - Swapping in kpage=%p from slot=%zu\n", kpage, slot);
    // lock_acquire(&swap_table.lock)

    // print("SWAP_IN: Reading page into kpage=%p from slot=%zu\n",
    //       kpage, slot)

    // base_sector = slot * sectors_per_page

    // // Read back into newly allocated frame
    // for i in range(0, sectors_per_page):
    //     block_read(swap_table.swap_block,
    //                base_sector + i,
    //                kpage + i * BLOCK_SECTOR_SIZE)

    // // Mark slot free again
    // bitmap_set(swap_table.used_slots, slot, false)

    // lock_release(&swap_table.lock)
}