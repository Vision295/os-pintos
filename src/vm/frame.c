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

void *frame_get_page(void *upage, struct thread *owner, bool zero) {
    // get a page from the allocated frame upage 
    ASSERT(owner != NULL);
    lock_acquire(&frame_table_lock);

    // Step 1: Allocate a physical frame
    void *kpage = frame_allocate(PAL_USER | (zero ? PAL_ZERO : 0));
    if (kpage == NULL) {
        // Eviction needed
        lock_release(&frame_table_lock);
        return NULL;
    }

    // Step 2: Associate frame with this virtual page and thread
    struct frame *f = malloc(sizeof(struct frame));
    if (f == NULL) {
        frame_free(kpage);
        lock_release(&frame_table_lock);
        return NULL;
    }
    f->kpage = kpage;   // pointer to physical frame
    // f->spte = f;     // supplemental page table entry for this virtual page
    f->owner = owner;   // thread that owns this frame
    f->upage = upage;   // virtual page mapped to this frame


    // Add the frame to the global frame table
    list_push_back(&frame_table, &f->elem);
    lock_release(&frame_table_lock);

    return f->kpage;
}