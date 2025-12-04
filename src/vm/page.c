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
    printf("[spt_insert] upage=%p\n", sp->upage);
    return hash_insert(spt, &sp->helem) == NULL;
}


struct spt_entry *spt_lookup(struct hash *spt, void *upage){
    printf("[spt_lookup] upage=%p\n", upage);
    struct spt_entry sp;
    sp.upage = pg_round_down(upage);
    struct hash_elem *e = hash_find(spt, &sp.helem);
    return e != NULL ? hash_entry(e, struct spt_entry, helem) : NULL;
}


// Helper function to grow the stack
static bool stack_grow(void *upage) {
    printf("[stack_grow] upage=%p\n", upage);
    struct thread *t = thread_current();
    
    struct frame *frame = frame_alloc(PAL_USER | PAL_ZERO, upage);
    if (frame == NULL) {
        printf("[stack_grow] frame_alloc failed\n");
        return false;
    }
    
    if (!install_page(upage, frame->kpage, true)) {
        printf("[stack_grow] install_page failed\n");
        frame_free(frame);
        return false;
    }
    
    struct spt_entry *spte = malloc(sizeof(struct spt_entry));
    if (spte == NULL) {
        printf("[stack_grow] malloc failed\n");
        pagedir_clear_page(t->pagedir, upage);
        frame_free(frame);
        return false;
    }
    
    spte->upage = upage;
    spte->loaded = true;
    spte->writable = true;
    spte->file = NULL;
    spte->ofs = 0;
    spte->read_bytes = 0;
    spte->zero_bytes = PGSIZE;
    spte->swap_slot = -1;
    spte->frame = frame;
    frame->spte = spte;
    
    if (!spt_insert(&t->spt, spte)) {
        printf("[stack_grow] spt_insert failed\n");
        pagedir_clear_page(t->pagedir, upage);
        frame_free(frame);
        free(spte);
        return false;
    }
    
    return true;
}



// Helper function to load a page from SPT
static bool load_page(struct spt_entry *spte) {
    printf("[load_page] upage=%p\n", spte->upage);

    if (spte->loaded) {
        printf("[load_page] already loaded\n");
        return true;
    }
    
    struct frame *frame = frame_alloc(PAL_USER, spte->upage);
    if (frame == NULL) {
        printf("[load_page] frame_alloc failed\n");
        return false;
    }
    
    void *kpage = frame->kpage;
    frame_pin(frame);

    if (spte->swap_slot != (size_t)-1) {
        // swap_in(spte->swap_slot, kpage);
        // spte->swap_slot = (size_t)-1;
    }
    else if (spte->file != NULL) {
        printf("[load_page] loading from file ofs=%d\n", spte->ofs);
        file_seek(spte->file, spte->ofs);
        int bytes_read = file_read(spte->file, kpage, spte->read_bytes);
        
        if (bytes_read != (int)spte->read_bytes) {
            printf("[load_page] file_read incomplete\n");
            frame_unpin(frame);
            frame_free(frame);
            return false;
        }
        
        memset(kpage + spte->read_bytes, 0, spte->zero_bytes);
    }
    else {
        memset(kpage, 0, PGSIZE);
    }

    if (!install_page(spte->upage, kpage, spte->writable)) {
        printf("[load_page] install_page failed\n");
        frame_unpin(frame);
        frame_free(frame);
        return false;
    }
    
    spte->frame = frame;
    frame->spte = spte;
    spte->loaded = true;
    
    frame_unpin(frame);

    return true;
}

bool page_fault_handle(void *fault_addr, bool write, struct intr_frame *f){
    printf("[pf_handler] fault_addr=%p write=%d\n", fault_addr, write);

    struct thread *t = thread_current();
    void *upage = pg_round_down(fault_addr);

    if (!is_user_vaddr(fault_addr) || fault_addr == NULL) {
        printf("[pf_handler] invalid user address\n");
        return false;
    }
    
    struct spt_entry *spte = spt_lookup(&t->spt, upage);
    if (spte != NULL && write && !spte->writable) {
        printf("[pf_handler] write to readonly page\n");
        return false;
    }
    
    void *esp = f->esp;
    
    bool is_stack_access = (fault_addr >= esp - STACK_GROWTH_LIMIT) &&
                           (PHYS_BASE - pg_round_down(fault_addr) <= MAX_STACK_SIZE);
    
    if (spte != NULL) {
        printf("[pf_handler] loading existing SPT entry\n");
        return load_page(spte);
    }
    else if (is_stack_access) {
        printf("[pf_handler] stack growth\n");
        return stack_grow(upage);
    }
    else {
        printf("[pf_handler] invalid access\n");
        return false;
    }
}



static void spt_destroy_func(struct hash_elem *e, void *aux UNUSED){
  struct spt_entry *spte = hash_entry(e, struct spt_entry, helem);
  free(spte);
}

void spt_destroy(struct hash *spt){
  printf("[spt_destroy]\n");
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
    printf("[spt_init]\n");
    hash_init(&thread_current()->spt, spt_hash, spt_less, NULL);
}
