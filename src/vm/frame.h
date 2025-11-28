#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <list.h>
#include "threads/palloc.h" 

extern struct list frame_table;
extern struct lock frame_table_lock;

struct frame {
    void *kpage;
    //void *upage;
    //struct thread *owner;
    //bool pinned;
    //size_t swap_slot;
    struct list_elem elem;
    struct spt_entry *spte;
};

void frame_init(void);
void *frame_allocate(enum palloc_flags flags);

void frame_free(void *kpage);

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