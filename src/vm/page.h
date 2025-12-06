#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <hash.h>
#include "threads/palloc.h" 
#include "filesys/off_t.h"
#include "threads/interrupt.h"

struct frame;
struct file;

struct spt_entry {
    void *upage;
    bool loaded;
    bool writable;     // whether it can be written
    struct file *file; // executable file backing this page (if any)
    off_t ofs;         // offset in the file
    size_t read_bytes; // bytes to read from file
    size_t zero_bytes; // number of bytes to be zeroed out at
                       // the end of a page because in the executable it is saved in
    size_t swap_slot;  // swap slot if swapped out
    struct hash_elem helem;
    struct frame *frame;
};

void spt_init(void);

bool spt_insert(struct hash *spt, struct spt_entry *sp);
struct spt_entry *spt_lookup(struct hash *spt, void *upage);
bool page_fault_handle(void *fault_addr, bool write, struct intr_frame *f);
void spt_destroy(struct hash *spt);
bool spt_remove(struct hash *spt, struct spt_entry *spte);
bool stack_grow(void *upage);
bool load_page(struct spt_entry *spte); 
bool is_valid_stack_access(void *fault_addr, void *esp);

#endif /* vm/page.h */