#include "page.h"
#include <hash.h>
#include <string.h>
#include "threads/thread.h"
#include <stdio.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "vm/frame.h"


bool spt_insert(struct hash *spt, struct spt_entry *sp) {
    return hash_insert(spt, &sp->helem) == NULL;
}


struct spt_entry *spt_lookup(struct hash *spt, void *upage){
    struct spt_entry sp;
    sp.upage = pg_round_down(upage);
    struct hash_elem *e = hash_find(spt, &sp.helem);
    return e != NULL ? hash_entry(e, struct spt_entry, helem) : NULL;
}

bool page_fault_handle(void *fault_addr, bool write){
    struct thread *t = thread_current();
    void *upage = pg_round_down(fault_addr);

    struct spt_entry *spte = spt_lookup(&t->spt, upage);
    if (spte == NULL)
        return false;  /* Not a valid page */
    
    if (write && !spte->writable)
        return false;  /* Write to read-only page */
    
    if (spte->loaded)
        return false;  /* Already loaded - shouldn't happen */
    
    /* Allocate frame */
    void *kpage = frame_allocate(PAL_USER);
    if (kpage == NULL)
        return false;  /* Out of memory - need eviction */
    
    /* Load from file */
    if (spte->file != NULL) {
        file_seek(spte->file, spte->ofs);
        if (file_read(spte->file, kpage, spte->read_bytes) != (int)spte->read_bytes) {
            frame_free(kpage);
            return false;
        }
        memset(kpage + spte->read_bytes, 0, spte->zero_bytes);
    } else {
        memset(kpage, 0, PGSIZE);
    }
    
    /* Install page */
    if (!pagedir_set_page(thread_current()->pagedir, upage, kpage, spte->writable)) {
    frame_free(kpage);
    return false;
    }
    // if (!install_page(upage, kpage, spte->writable)) {
    //     frame_free(kpage);
    //     return false;
    // }
    
    spte->loaded = true;
    return true;
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

bool load_page(struct spt_entry *spte) {
    // if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
    //   {
    //     frame_free(kpage);
    //     //palloc_free_page (kpage);
    //     return false; 
    //   }

    struct frame* f = frame_get_page(spte->upage, thread_current(), false);

    frame_pin(f);

    // if spte.is_file_backed:
    //     read_from_file_into(f.kpage, spte.file, spte.offset)
    // else if spte.is_swapped:
    //     swap_in(f.kpage, spte.swap_slot)
    // install_page_mapping(f.owner, f.upage, f.kpage)

    spte->loaded = true;
    spte->frame = f;
    frame_unpin(f);

    return true;
}