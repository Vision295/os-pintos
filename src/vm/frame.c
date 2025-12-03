#include "frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"

#include "vm/page.h"
#include "vm/swap.h"

#include <debug.h>
#include <stdio.h>

struct list frame_table;
struct lock frame_table_lock;
size_t clock_hand;

void frame_init(void){
    list_init(&frame_table);
    lock_init(&frame_table_lock);
    printf("[frame_init] - Frame table initialized\n");
}


// Allocate a frame with eviction support
// Returns frame pointer or NULL if allocation fails
struct frame *frame_alloc(enum palloc_flags flags, void *upage) {
    ASSERT(flags & PAL_USER);
    
    struct thread *owner = thread_current();
    ASSERT(owner != NULL);
    
    lock_acquire(&frame_table_lock);
    
    // Try to allocate physical page
    void *kpage = palloc_get_page(flags);
    
    // If allocation failed, try eviction once
    if (kpage == NULL) {
        lock_release(&frame_table_lock);
        frame_eviction();
        lock_acquire(&frame_table_lock);
        
        kpage = palloc_get_page(flags);
        if (kpage == NULL) {
            lock_release(&frame_table_lock);
            return NULL;
        }
    }
    
    // Allocate frame structure
    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL) {
        palloc_free_page(kpage);
        lock_release(&frame_table_lock);
        return NULL;
    }
    
    // Initialize frame
    f->kpage = kpage;
    f->upage = upage;
    f->owner = owner;
    f->pinned = false;
    f->spte = spt_lookup(&owner->spt, upage);
    
    list_push_back(&frame_table, &f->elem);
    
    lock_release(&frame_table_lock);
    
    return f;
}

// Free a frame by frame pointer
void frame_free(struct frame *frame) {
    ASSERT(frame != NULL);

    lock_acquire(&frame_table_lock);
    list_remove(&frame->elem);
    lock_release(&frame_table_lock);

    palloc_free_page(frame->kpage);
    free(frame);
}


void frame_pin(struct frame *f) {
    ASSERT(f != NULL);

    lock_acquire(&frame_table_lock);
    f->pinned = true;
    lock_release(&frame_table_lock);

    printf("[frame_pin] - Pinned frame for upage=%p\n", f->upage);
}

void frame_unpin(struct frame *f) {
    ASSERT(f != NULL);

    lock_acquire(&frame_table_lock);
    f->pinned = false;
    lock_release(&frame_table_lock);

    printf("[frame_unpin] - Unpinned frame for upage=%p\n", f->upage);
}

bool frame_eviction(void) {
    printf("[frame_eviction] - Choosing victim frame\n");

    struct frame *victim = frame_choose_victim();
    if (victim == NULL) {
        printf("[frame_eviction] - No victim found\n");
        return false;
    }

    printf("[frame_eviction] - Victim found, evicting...\n");
    return frame_evict(victim);
}

struct frame *
get_frame_at(size_t index) {
    struct list_elem *e = list_begin(&frame_table);
    for (size_t i = 0; i < index; i++) {
        e = list_next(e);
    }
    return list_entry(e, struct frame, elem);
}

void
advance_clock_hand(void) {
    clock_hand = (clock_hand + 1) % list_size(&frame_table);
}

struct frame *
frame_choose_victim(void) {
    ASSERT(!list_empty(&frame_table));
    printf("[frame_choose_victim] - Starting clock scan\n");

    size_t n = list_size(&frame_table);
    lock_acquire(&frame_table_lock);

    while (true) {
        struct frame *f = get_frame_at(clock_hand);
        bool accessed = pagedir_is_accessed(f->owner->pagedir, f->upage);

        if (accessed || f->pinned) {
            // Give second chance
            pagedir_set_accessed(f->owner->pagedir, f->upage, false);
            advance_clock_hand();
            clock_hand %= n;
        } else {
            struct frame *victim = f;
            advance_clock_hand();
            lock_release(&frame_table_lock);

            printf("[frame_choose_victim] - Victim: upage=%p\n", victim->upage);
            return victim;
        }
    }
}

bool frame_evict(struct frame *victim) {
    ASSERT(victim != NULL);
    ASSERT(victim->spte != NULL);

    struct spt_entry *spte = victim->spte;
    struct thread *owner  = victim->owner;
    void *upage           = victim->upage;
    void *kpage           = victim->kpage;

    printf("[frame_evict] - Evicting upage=%p\n", upage);

    if (victim->pinned) {
        printf("[frame_evict] - ABORT: frame is pinned\n");
        return false;
    }

    bool dirty = pagedir_is_dirty(owner->pagedir, upage);

    if (spte->file != NULL) {
        if (dirty) {
            printf("[frame_evict] - Writing dirty file page\n");
            lock_acquire(&frame_table_lock);
            file_write_at(spte->file, kpage, spte->read_bytes, spte->ofs);
            lock_release(&frame_table_lock);
        }
        spte->loaded = false;
        spte->frame  = NULL;
    } else {
        printf("[frame_evict] - Swapping out page\n");
        // size_t slot = swap_out(kpage);
        // spte->swap_slot = slot;
        spte->loaded = false;
        spte->frame  = NULL;
    }

    pagedir_clear_page(owner->pagedir, upage);

    lock_acquire(&frame_table_lock);
    list_remove(&victim->elem);
    lock_release(&frame_table_lock);

    palloc_free_page(kpage);
    free(victim);

    printf("[frame_evict] - Eviction complete\n");
    return true;
}