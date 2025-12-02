#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <list.h>
#include "threads/palloc.h" 

extern struct list frame_table;
extern struct lock frame_table_lock;

struct frame {
    void *kpage;            // kernel pool page
    void *upage;            // user pool page
    struct thread *owner; 
    // bool pinned;            // for I/O 
    // size_t swap_slot;       // if frame evicted, store swap slot index 
    struct list_elem elem; 
    struct spt_entry *spte; 
};

void frame_init(void);
//void *frame_allocate(enum palloc_flags flags);

struct frame *frame_alloc(enum palloc_flags flags, void *upage);

void frame_free(struct frame *frame);

//void *frame_get_page(void *upage, struct thread *owner, bool zero);


// Eviction
//struct frame *frame_choose_victim(void);
//void frame_pin(struct frame *f);
//void frame_unpin(struct frame *f);

// Maybe not needed
//struct frame *frame_lookup_kpage(void *kpage);
//struct frame *frame_lookup_upage(struct thread *t, void *upage);
//void frame_reuse(struct frame *victim, void *upage, struct thread *owner, bool zero);


#endif /* vm/frame.h */