#include "page.h"
#include <hash.h>
#include <string.h>
#include "threads/thread.h"
#include <stdio.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "vm/frame.h"

#define MAX_STACK_SIZE (8 * 1024 * 1024)
#define STACK_GROWTH_LIMIT 32

bool spt_insert(struct hash *spt, struct spt_entry *sp) {
    return hash_insert(spt, &sp->helem) == NULL;
}


struct spt_entry *spt_lookup(struct hash *spt, void *upage){
    struct spt_entry sp;
    sp.upage = pg_round_down(upage);
    struct hash_elem *e = hash_find(spt, &sp.helem);
    return e != NULL ? hash_entry(e, struct spt_entry, helem) : NULL;
}


// Helper function to grow the stack
static bool stack_grow(void *upage) {
    struct thread *t = thread_current();
    
    // Allocate a frame
    struct frame *frame = frame_alloc(PAL_USER | PAL_ZERO, upage);
    if (frame == NULL) {
        return false;
    }
    
    // Install the page in the page table
    if (!install_page(upage, frame->kpage, true)) {
        frame_free(frame);
        return false;
    }
    
    // Create and add entry to supplemental page table
    struct spt_entry *spte = malloc(sizeof(struct spt_entry));
    if (spte == NULL) {
        pagedir_clear_page(t->pagedir, upage);
        frame_free(frame);
        return false;
    }
    
    // Initialize SPT entry for stack page
    spte->upage = upage;
    spte->loaded = true;
    spte->writable = true;
    spte->file = NULL;
    spte->ofs = 0;
    spte->read_bytes = 0;
    spte->zero_bytes = PGSIZE;
    spte->swap_slot = -1;  // or SIZE_MAX if using size_t
    spte->frame = frame;
    frame->spte = spte;
    
    // Insert into SPT
    if (!spt_insert(&t->spt, spte)) {
        pagedir_clear_page(t->pagedir, upage);
        frame_free(frame);
        free(spte);
        return false;
    }
    
    return true;
}



// Helper function to load a page from SPT
static bool load_page(struct spt_entry *spte) {
    //struct thread *t = thread_current();
    
    // Check if already loaded
    if (spte->loaded) {
        return true;
    }
    
    // Allocate frame
    struct frame *frame = frame_alloc(PAL_USER, spte->upage);
    if (frame == NULL) {
        return false;  // Could trigger eviction here
    }
    
    void *kpage = frame->kpage;
    
    // Determine where to load from
    if (spte->swap_slot != (size_t)-1) {
        // Load from swap
        //swap_in(spte->swap_slot, kpage);
        //spte->swap_slot = (size_t)-1;
    }
    else if (spte->file != NULL) {
        // Load from file
        file_seek(spte->file, spte->ofs);
        int bytes_read = file_read(spte->file, kpage, spte->read_bytes);
        
        if (bytes_read != (int)spte->read_bytes) {
            frame_free(frame);
            return false;
        }
        
        // Zero out remaining bytes
        memset(kpage + spte->read_bytes, 0, spte->zero_bytes);
    }
    else {
        // Zero-filled page (anonymous memory)
        memset(kpage, 0, PGSIZE);
    }

    // Install page in page table
    if (!install_page(spte->upage, kpage, spte->writable)) {
        frame_free(frame);
        return false;
    }
    
    // Update SPT entry
    spte->frame = frame;
    frame->spte = spte;
    spte->loaded = true;
    
    return true;
}

bool page_fault_handle(void *fault_addr, bool write, struct intr_frame *f){
    struct thread *t = thread_current();
    void *upage = pg_round_down(fault_addr);
    
    // 1. Validate the fault address
    if (!is_user_vaddr(fault_addr) || fault_addr == NULL) {
        return false;
    }
    
    // 2. Check for write to read-only page
    struct spt_entry *spte = spt_lookup(&t->spt, upage);
    if (spte != NULL && write && !spte->writable) {
        return false;  // Writing to read-only page
    }
    
    // 3. Check if this is a valid stack growth scenario
    #define MAX_STACK_SIZE (8 * 1024 * 1024)  // 8 MB
    #define STACK_GROWTH_LIMIT 32  // bytes below esp for PUSH/PUSHA
    
    void *esp = f->esp;
    
    bool is_stack_access = (fault_addr >= esp - STACK_GROWTH_LIMIT) &&
                           (PHYS_BASE - pg_round_down(fault_addr) <= MAX_STACK_SIZE);
    
    // 4. Handle based on whether page exists in SPT
    if (spte != NULL) {
        // Page exists in SPT - load it
        return load_page(spte);
    }
    else if (is_stack_access) {
        // Valid stack growth - allocate new stack page
        return stack_grow(upage);
    }
    else {
        // Invalid memory access
        return false;
    }
}






static void spt_destroy_func(struct hash_elem *e, void *aux UNUSED){
  struct spt_entry *spte = hash_entry(e, struct spt_entry, helem);
  free(spte);
}

// SPT
void spt_destroy(struct hash *spt){
  hash_destroy(spt, spt_destroy_func);
}


static unsigned spt_hash(const struct hash_elem *e, void *aux UNUSED) {
    const struct spt_entry *sp = hash_entry(e, struct spt_entry, helem);
    return hash_bytes(&sp->upage, sizeof sp->upage);
}

static bool spt_less(const struct hash_elem *a, const struct hash_elem *b,
                     void *aux UNUSED) {
    const struct spt_entry *sa = hash_entry(a, struct spt_entry, helem);
    const struct spt_entry *sb = hash_entry(b, struct spt_entry, helem);
    return sa->upage < sb->upage;
}

void spt_init() {
    hash_init(&thread_current()->spt, spt_hash, spt_less, NULL);
}