#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <list.h>
#include "threads/palloc.h" 

extern struct list frame_table;
extern struct lock frame_table_lock;
extern size_t clock_hand;

struct frame {
    void *kpage;            // kernel pool page
    void *upage;            // user pool page
    struct thread *owner; 
    bool pinned;            // for I/O 
    // size_t swap_slot;    // if frame evicted, store swap slot index 
    struct list_elem elem; 
    struct spt_entry *spte; 
};

void frame_init(void);
void *frame_allocate(enum palloc_flags flags);

void frame_free(void *kpage);

struct frame *frame_get_page(void *upage, struct thread *owner, bool zero);


// Eviction
bool frame_eviction(void);
struct frame *frame_choose_victim(void);
bool frame_evict(struct frame *victim);
void frame_pin(struct frame *f);
void frame_unpin(struct frame *f);
struct frame* get_frame_at(size_t index);
void advance_clock_hand(void);

// Maybe not needed
//struct frame *frame_lookup_kpage(void *kpage);
//struct frame *frame_lookup_upage(struct thread *t, void *upage);
//void frame_reuse(struct frame *victim, void *upage, struct thread *owner, bool zero);


#endif /* vm/frame.h */