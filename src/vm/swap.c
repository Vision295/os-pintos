#include "devices/block.h"
#include "bitmap.h"
#include "vm/swap.h"

#include <stdio.h>
#include <string.h>

#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"


static struct block *swap_block;
static struct bitmap *swap_bitmap;
static struct lock swap_lock;

static size_t SECTORS_PER_PAGE;

/* Initialize swap system */
void swap_init(void) {
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL)
        PANIC("No swap block!");

    SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

    size_t swap_size = block_size(swap_block);
    size_t slot_count = swap_size / SECTORS_PER_PAGE;

    swap_bitmap = bitmap_create(slot_count);
    if (swap_bitmap == NULL)
        PANIC("Could not create swap bitmap");

    bitmap_set_all(swap_bitmap, false);

    lock_init(&swap_lock);
}

/* Swap out a page -> returns slot index */
size_t swap_out(void *kpage) {
    ASSERT(pg_ofs(kpage) == 0);
    ASSERT(swap_bitmap != NULL);
    ASSERT(swap_block != NULL);

    lock_acquire(&swap_lock);

    size_t slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
    if (slot == BITMAP_ERROR) {
        lock_release(&swap_lock);
        PANIC("SWAP FULL");
    }

    size_t base_sector = slot * SECTORS_PER_PAGE;
    uint8_t *buf = (uint8_t *) kpage;

    for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
        block_write(swap_block,
                    base_sector + i,
                    buf + i * BLOCK_SECTOR_SIZE);

    lock_release(&swap_lock);

    return slot;
}

/* Swap in: read slot → restore page to memory */
void swap_in(void *kpage, size_t slot) {
    ASSERT(pg_ofs(kpage) == 0);
    ASSERT(swap_bitmap != NULL);
    ASSERT(swap_block != NULL);
    ASSERT(slot < bitmap_size(swap_bitmap));

    lock_acquire(&swap_lock);

    ASSERT(bitmap_test(swap_bitmap, slot));

    size_t base_sector = slot * SECTORS_PER_PAGE;
    uint8_t *buf = (uint8_t *) kpage;

    for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
        block_read(swap_block,
                   base_sector + i,
                   buf + i * BLOCK_SECTOR_SIZE);

    bitmap_set(swap_bitmap, slot, false);

    lock_release(&swap_lock);
}

/* Free swap slot without reading */
void swap_free(size_t slot) {
    ASSERT(slot < bitmap_size(swap_bitmap));
    bitmap_set(swap_bitmap, slot, false);
}
