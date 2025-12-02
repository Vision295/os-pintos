#include "frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <debug.h>

struct list frame_table;
struct lock frame_table_lock;

void frame_init(void){
    list_init(&frame_table);
    lock_init(&frame_table_lock);
    return;
}

void *frame_allocate(enum palloc_flags flags){
    ASSERT(flags & PAL_USER);

    void *kpage = palloc_get_page(flags);
    if (kpage == NULL){
        return NULL;
    }

    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL){
        palloc_free_page(kpage);
        return NULL;
    }

    f->kpage = kpage;
    f->spte = NULL;

    lock_acquire(&frame_table_lock);
    list_push_back(&frame_table, &f->elem);
    lock_release(&frame_table_lock);

    return kpage;
}

void frame_free(void *kpage) {
    ASSERT(kpage != NULL);

    lock_acquire(&frame_table_lock);

    struct list_elem *e;
    struct frame *f = NULL;

    for (e = list_begin(&frame_table);
        e != list_end(&frame_table);
        e = list_next(e)){
            struct frame *temp = list_entry(e, struct frame, elem);
            if (temp->kpage == kpage){
                f = temp;
                break;
            }
        }
    if (f != NULL){
        list_remove(&f->elem);
        lock_release(&frame_table_lock);

        palloc_free_page(kpage);
        free(f);
    } else {
        lock_release(&frame_table_lock);
        PANIC("Attempted to free non existent frame");
    }
}

struct frame *frame_get_page(void *upage, struct thread *owner, bool zero) {
    // get a page from the allocated frame upage 
    ASSERT(owner != NULL);
    lock_acquire(&frame_table_lock);

    // Step 1: Allocate a physical frame
    void *kpage = frame_allocate(PAL_USER | (zero ? PAL_ZERO : 0));
    if (kpage == NULL) {
        // TODO: handle the bool
        frame_eviction();
        lock_release(&frame_table_lock);
        return NULL;
        if (kpage == NULL) {
            printf("Frame allocation failed during eviction.\n");
            lock_release(&frame_table_lock);
            return NULL;
        }
    }

    // Step 2: Associate frame with this virtual page and thread
    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL) {
        frame_free(kpage);
        lock_release(&frame_table_lock);
        return NULL;
    }
    f->kpage = kpage;   // pointer to physical frame
    f->upage = upage;   // pointer to physical frame
    f->owner = owner;   // thread that owns this frame
    f->upage = upage;   // virtual page mapped to this frame
    f->pinned = false;  // not I/O by default
    f->spte = spt_lookup(&owner->spt, upage);     // supplemental page table entry for this virtual page


    // Add the frame to the global frame table
    list_push_back(&frame_table, &f->elem);
    lock_release(&frame_table_lock);

    return f->kpage;
}

void frame_pin(struct frame *f) {
    ASSERT(f != NULL);
    lock_acquire(&frame_table_lock);
    f->pinned = true;
    lock_release(&frame_table_lock);
}

void frame_unpin(struct frame *f) {
    ASSERT(f != NULL);
    lock_acquire(&frame_table_lock);
    f->pinned = false;
    lock_release(&frame_table_lock);
}

bool frame_eviction(void) {
    // Evict a frame
    struct frame *victim = frame_choose_victim();
    if (victim == NULL) {
        return false; // No suitable victim found
    }
    return frame_evict(victim);
}

struct frame *frame_choose_victim(void) {
    // acquire frame_table_lock

    // loop forever:
    //     f = frame_table[clock_hand]
    //     clock_hand = (clock_hand + 1) % frame_table.size

    //     if f.pinned == true:
    //         continue  // pinned → skip

    //     if accessed_bit(f.owner, f.upage) == 1:
    //         clear_accessed_bit(f.owner, f.upage)
    //         continue  // second chance → skip

    //     // Found a suitable victim
    //     release frame_table_lock
    //     return f

}

bool frame_evict(struct frame *victim) {
    ASSERT(victim != NULL)

    struct spt_entry *spte = victim->spte;
    struct thread *owner = victim->owner;
    void *upage = victim->upage;
    void *kpage = victim->kpage;

    // // Step 1: Write page contents out
    // if spte.is_file_backed:
    //     if page_is_dirty(owner, upage):
    //         write_page_back_to_file(spte.file, spte.offset, kpage)
    // else:
    //     // anonymous or stack page
    //     spte.swap_slot = swap_out(kpage)
    //     spte.is_swapped = true

    // // Step 2: Remove victim page from thread's pagedir
    // clear_pte(owner, upage)

    // // Step 3: Remove frame struct from table + free it
    // acquire frame_table_lock
    // list_remove(&victim->elem)
    // release frame_table_lock

    // palloc_free_page(kpage)
    // free(victim)
}