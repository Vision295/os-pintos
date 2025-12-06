#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"


#define MAX_STACK_SIZE (8 * 1024 * 1024)
static void syscall_handler (struct intr_frame *);
static void check_user_pointer (const void *uaddr);

struct lock filesys_lock;
/* Validate that a user pointer is in user space and mapped.
   If not, terminate the process. */
static void
check_user_pointer (const void *uaddr) {
  struct thread *t = thread_current ();
  if (uaddr == NULL ||
      !is_user_vaddr (uaddr) ||
      pagedir_get_page (t->pagedir, uaddr) == NULL)
    exit (-1);
}

/* Validate that a user pointer range [uaddr, uaddr + size - 1] is in user space and mapped.
   If not, terminate the process. */
static void
check_user_pointer_range(const void *uaddr, size_t size) { 
    //struct thread *t = thread_current();
    
    // Handle empty buffer case
    if (size == 0) return;
    
    //const void *start = uaddr;
    //const void *end = (const char *)uaddr + size - 1;
    // NULL check and verify both start and end are in user space
    //if (start == NULL || !is_user_vaddr(start) || !is_user_vaddr(end)) exit(-1);
    if (uaddr == NULL || 
        !is_user_vaddr(uaddr) || 
        !is_user_vaddr((const char *)uaddr + size - 1)) {
        exit(-1);
    }
    // // Iterate over pages within the range
    // const void *current_ptr = pg_round_down(start); // Start at page boundary
    // const void *end_page = pg_round_down(end);       // End page boundary
    
    // while (current_ptr <= end_page) {
    //     // Check if the page is mapped in the process's page directory
    //     if (pagedir_get_page(t->pagedir, current_ptr) == NULL) exit(-1);
        
    //     // Move to the next page
    //     current_ptr = (const char *)current_ptr + PGSIZE;
    // }
    // ADD THIS: Load all pages in the range
    struct thread *t = thread_current();
    const void *page = pg_round_down(uaddr);
    const void *end = pg_round_down((const char *)uaddr + size - 1);
    
    while (page <= end) {
        struct spt_entry *spte = spt_lookup(&t->spt, (void *)page);
        
        if (spte != NULL && !spte->loaded) {
            // Load the page NOW before kernel accesses it
            if (!load_page(spte)) {  // Your load_page function
                exit(-1);
            }
        } else if (spte == NULL) {
            // Could be stack - check if valid stack access
            //void *esp = t->user_esp;  // Use saved user ESP!
            printf("Need to grow stack\n");
            // if (page >= esp - STACK_GROWTH_LIMIT && 
            //     PHYS_BASE - page <= MAX_STACK_SIZE) {
            //     if (!stack_grow((void *)page)) {
            //         exit(-1);
            //     }
            // } else {
            //     exit(-1);  // Invalid access
            // }
        }
        // If spte is NULL, it might be stack - let page fault handle it
        // Or you could call stack_grow here like the working code
        
        page = (const char *)page + PGSIZE;
    }
}


/* Validate that a user string is in user space and mapped.
   If not, terminate the process. */
static void check_user_string(const char *str) {
    // Check if the string pointer itself is valid first
    check_user_pointer(str);
    
    // Then iterate through the string
    const char *current = str;
    while (true) {
        // Check if we've crossed into a new page
        if (pg_round_down(current) != pg_round_down(current - 1) && current != str) {
            check_user_pointer(current);
        }
        
        if (*current == '\0') {
            break;
        }
        current++;
    }
}



/* Shut down the machine. */
void
halt (void) {
  shutdown_power_off ();
}

/* Terminate the current user program with status. */
void
exit (int status) {
  struct thread *cur = thread_current ();
  cur->child_info->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

/* Start another process. */
pid_t
exec (const char *cmd_line) {
  check_user_pointer (cmd_line);
  pid_t pid = process_execute (cmd_line);

  // if wrong arguments, name, executable, etc.
  if (pid == TID_ERROR)
    return -1;

  struct child_info *info = thread_find_child (pid);
  // If process is not a child
  if (!info)
    return -1;

  // avoid race condition
  sema_down (&info->load_sema);
  if (!info->load_success)
    return -1;

  return pid;
}

/* Wait for child to finish. */
int
wait (pid_t pid) {
  return process_wait (pid);
}

/* File operations */
bool
create (const char *file, unsigned initial_size) {
  check_user_pointer (file);
  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

bool
remove (const char *file) {
  check_user_pointer (file);
  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

int
open (const char *file) {
  check_user_pointer (file);

  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  lock_release (&filesys_lock);

  if (f == NULL)
    return -1;

  // Deny write if the file is the executable of the current process
  // to avoid to open the executable for writing
  struct thread *cur = thread_current();
  if(cur->executable != NULL){
    if(file_get_inode(f) == file_get_inode(cur->executable)){
      file_deny_write(f);
    }
  }

  // Get safely the next available file descriptor
  int fd = thread_get_fd ();
  //printf("[syscall] open('%s') = %d\n", file, fd);
  if (fd == -1) {
    file_close (f);
    return -1;
  }
  thread_current ()->fd_table[fd] = f;
  return fd;
}

int
filesize (int fd) {
  // if not stdin or stdout
  if (fd < 2 || fd >= MAX_FD)
    return -1;
  struct file *f = thread_current ()->fd_table[fd];
  if (f == NULL)
    return -1;

  return file_length (f);
}


int
read (int fd, void *buffer, unsigned size) {
  check_user_pointer_range(buffer, size);                 // read/write buffers

  int bytes_read = 0;
  lock_acquire (&filesys_lock);

  if (fd == 0) {  /* stdin */
    // Read from keyboard input
    for (unsigned i = 0; i < size; i++)
      ((uint8_t *) buffer)[i] = input_getc ();
    bytes_read = size;
  } else if (fd > 1 && fd < MAX_FD) {
    // Read from file
    struct file *f = thread_current ()->fd_table[fd];
    if (f)
      bytes_read = file_read (f, buffer, size);
  }

  lock_release (&filesys_lock);
  return bytes_read;
}


int
write (int fd, const void *buffer, unsigned size) {
  check_user_pointer_range(buffer, size);                 // read/write buffers

  int bytes_written = 0;
  lock_acquire (&filesys_lock);

  if (fd == 1) { 
    // Write to console output
    putbuf (buffer, size);
    bytes_written = size;
  } else if (fd > 1 && fd < MAX_FD) {
    // Write to file
    struct file *f = thread_current ()->fd_table[fd];
    if (f){
      bytes_written = file_write (f, buffer, size);
    }
      
  }

  lock_release (&filesys_lock);
  return bytes_written;
}


void
seek (int fd, unsigned position) {
  if (fd < 2 || fd >= MAX_FD)
    return;
  struct file *f = thread_current ()->fd_table[fd];
  if (f)
    file_seek (f, position);
}


unsigned
tell (int fd) {
  if (fd < 2 || fd >= MAX_FD)
    return -1;
  struct file *f = thread_current ()->fd_table[fd];
  if (f == NULL)
    return -1;

  return file_tell (f);
}

void
close (int fd) {
  if (fd < 2 || fd >= MAX_FD)
    return;
  struct file *f = thread_current ()->fd_table[fd];
  if (f == NULL)
    return;

  file_close (f);
  thread_current ()->fd_table[fd] = NULL;
}

static void mmap_cleanup(struct thread *t, struct mmap_entry *entry) {
    void *last_page = entry->addr + ((entry->length + PGSIZE - 1) & ~(PGSIZE - 1));
    void *page_addr = entry->addr;

    while (page_addr < last_page) {
        struct spt_entry *spte = spt_lookup(&t->spt, page_addr);
        if (spte != NULL) {
            // Write back if dirty
            if (spte->frame != NULL && pagedir_is_dirty(t->pagedir, spte->upage)) {
                file_seek(entry->file, spte->ofs);
                file_write(entry->file, spte->frame->kpage, spte->read_bytes);
            }

            // Remove from SPT and free frame/spte
            spt_remove(&t->spt, spte);
            if (spte->frame != NULL)
                frame_free(spte->frame);
            free(spte);
        }
        page_addr += PGSIZE;
    }

    // Remove mmap_entry from list and close file
    list_remove(&entry->elem);
    file_close(entry->file);
    free(entry);
}




mapid_t mmap(int fd, void *addr) {
    if (fd < 2 || fd >= MAX_FD){
      return -1; 
    }
    struct thread *t = thread_current();
    struct file *file;
    size_t file_size;
    size_t ofs = 0;
    mapid_t mapid;
    
    // 1. Validate input
    if (addr == NULL || pg_ofs(addr) != 0) // not page-aligned
    {
      return -1;
    }    
    

    lock_acquire(&filesys_lock);
    
    file = thread_current ()->fd_table[fd];
    if (file == NULL)
    {
      return -1;
    }    

    file = file_reopen(file);  // Add after getting file from fd
    if (file == NULL)
    {
      return -1;
    }    

    file_size = file_length(file);
    if (file_size == 0){
      file_close(file);
      return -1;
    }
        
    lock_release(&filesys_lock);
    // 2. Check for overlap with existing SPT pages
    size_t size_left = file_size;
    void *page_addr = addr;
    while (size_left > 0) {
        if (spt_lookup(&t->spt, page_addr) != NULL){
          file_close(file);    ;
          return -1;
        }
        
        page_addr += PGSIZE;
        size_left = (size_left > PGSIZE) ? size_left - PGSIZE : 0;
    }

    // 3. Create a new mmap_entry
    struct mmap_entry *entry = malloc(sizeof(struct mmap_entry));
    if (entry == NULL)
        return -1;

    mapid = ++t->next_mapid;
    entry->id = mapid;
    entry->file = file;
    entry->addr = addr;
    entry->length = file_size;
    list_push_back(&t->mmap_list, &entry->elem);
    
    // 4. Create SPT entries for each page
    size_left = file_size;
    page_addr = addr;
    ofs = 0;

    while (size_left > 0) {
        struct spt_entry *spte = malloc(sizeof(struct spt_entry));
        if (spte == NULL) {
          mmap_cleanup(t, entry);
          return -1;
        }

        size_t read_bytes = (size_left > PGSIZE) ? PGSIZE : size_left;
        size_t zero_bytes = PGSIZE - read_bytes;

        spte->upage = page_addr;
        spte->loaded = false;
        spte->writable = true;      // mmap is usually writable
        spte->file = file;
        spte->ofs = ofs;
        spte->read_bytes = read_bytes;
        spte->zero_bytes = zero_bytes;
        spte->swap_slot = -1;
        spte->frame = NULL;

        if (!spt_insert(&t->spt, spte)) {
          mmap_cleanup(t, entry);  
            free(spte);
            return -1;
        }

        page_addr += PGSIZE;
        ofs += read_bytes;
        size_left -= read_bytes;
    }

    // 5. Return the new mapping id
    return mapid;
}

static struct mmap_entry *find_mmap_entry(struct thread *t, mapid_t mapid) {
    struct list_elem *e;
    
    for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list);
         e = list_next(e)) {
        struct mmap_entry *entry = list_entry(e, struct mmap_entry, elem);
        if (entry->id == mapid)
            return entry;
    }
    return NULL;
}

void munmap(mapid_t mapping) {
    struct thread *t = thread_current();
    struct mmap_entry *entry;
    
    entry = find_mmap_entry(t, mapping);
    if (entry == NULL) {
        return;
    }
    
    void *page_addr = entry->addr;
    size_t size_left = entry->length;
    
    while (size_left > 0) {
        struct spt_entry *spte = spt_lookup(&t->spt, page_addr);
        
        if (spte != NULL) {
            if (spte->loaded && spte->frame != NULL) {                
                if (pagedir_is_dirty(t->pagedir, spte->upage)) {
                    lock_acquire(&filesys_lock);
                    file_seek(entry->file, spte->ofs);
                    file_write(entry->file, spte->upage, spte->read_bytes);
                    lock_release(&filesys_lock);
                }
                
                frame_free(spte->frame);
                pagedir_clear_page(t->pagedir, spte->upage);
            }
            
            spt_remove(&t->spt, spte);
            free(spte);
        }
        
        page_addr += PGSIZE;
        size_left = (size_left > PGSIZE) ? size_left - PGSIZE : 0;
    }
    
    lock_acquire(&filesys_lock);
    file_close(entry->file);
    lock_release(&filesys_lock);
    
    list_remove(&entry->elem);
    free(entry);
}





/* System call initialization. */
void
syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

/* System call handler */
static void
syscall_handler (struct intr_frame *f) {
  /*  1. read arguments from the frame
      2. check their validity 
      3. perform the system call  */
  uint32_t *user_esp = f->esp;
  thread_current()->esp = f->esp;
  thread_current()->saved_esp = f->esp;
  /* First, validate the stack pointer itself */
  check_user_pointer (user_esp);

  /* Validate we can read the syscall number (a 4-byte int) */
  /* This checks that all 4 bytes of the syscall number are accessible */
  check_user_pointer_range(user_esp, sizeof(int));

  int syscall_number = *user_esp;

  switch (syscall_number) {
    case SYS_HALT:
      halt ();
      break;

    case SYS_EXIT: {
      /* Validate the entire argument (4 bytes) */
      check_user_pointer_range(user_esp + 1, sizeof(int));
      int status = *(int *)(user_esp + 1);
      exit (status);
      break;
    }

    case SYS_EXEC: {
      /* Validate all 4 bytes of the pointer argument */
      check_user_pointer_range(user_esp + 1, sizeof(char *));

      /* Now safe to read the pointer */
      const char *cmd_line = *(const char **)(user_esp + 1);
      /* Validate the string that the pointer points to */
      check_user_string (cmd_line);

      f->eax = exec (cmd_line);
      break;
    }

    case SYS_WAIT: {
      check_user_pointer_range(user_esp + 1, sizeof(pid_t));

      pid_t pid = *(pid_t *)(user_esp + 1);
      f->eax = wait (pid);
      break;
    }

    case SYS_CREATE: {
      /* Validate pointer argument (4 bytes) and size argument (4 bytes) */
      check_user_pointer_range(user_esp + 1, sizeof(char *));
      check_user_pointer_range(user_esp + 2, sizeof(unsigned));
      
      const char *file = *(const char **)(user_esp + 1);
      unsigned initial_size = *(unsigned *)(user_esp + 2);
      check_user_string (file);

      f->eax = create (file, initial_size);
      break;
    }

    case SYS_REMOVE: {
      check_user_pointer_range(user_esp + 1, sizeof(char *));

      const char *file = *(const char **)(user_esp + 1);
      check_user_string (file);

      f->eax = remove (file);
      break;
    }

    case SYS_OPEN: {
      check_user_pointer_range(user_esp + 1, sizeof(char *));
      
      const char *file = *(const char **)(user_esp + 1);
      check_user_string (file);

      f->eax = open (file);
      break;
    }

    case SYS_FILESIZE: {
      check_user_pointer_range(user_esp + 1, sizeof(int));

      int fd = *(int *)(user_esp + 1);
      f->eax = filesize (fd);
      break;
    }

    case SYS_READ: {
      /* Validate all three arguments before dereferencing */
      check_user_pointer_range(user_esp + 1, sizeof(int));
      check_user_pointer_range(user_esp + 2, sizeof(void *));
      check_user_pointer_range(user_esp + 3, sizeof(unsigned));

      /* Now it's safe to read the arguments */
      int fd = *(int *)(user_esp + 1);
      void *buffer = *(void **)(user_esp + 2);
      unsigned size = *(unsigned *)(user_esp + 3);
      
      /* The read() function will validate the buffer */
      //printf("[syscall] read(fd=%d, buffer=%p, size=%u)\n", fd, buffer, size);
      
      //f->eax = read (fd, buffer, size);
      int result = read (fd, buffer, size);
      f->eax = result;
      //printf("[syscall] read() returned %d\n", result);
      break;
    }

    case SYS_WRITE: {
      /* Validate all three arguments before dereferencing */
      check_user_pointer_range(user_esp + 1, sizeof(int));
      check_user_pointer_range(user_esp + 2, sizeof(void *));
      check_user_pointer_range(user_esp + 3, sizeof(unsigned));

      int fd = *(int *)(user_esp + 1);
      const void *buffer = *(const void **)(user_esp + 2);
      unsigned size = *(unsigned *)(user_esp + 3);
      
      /* The write() function will validate the buffer */
      f->eax = write (fd, buffer, size);
      break;
    }

    case SYS_SEEK: {
      check_user_pointer_range(user_esp + 1, sizeof(int));
      check_user_pointer_range(user_esp + 2, sizeof(unsigned));

      int fd = *(int *)(user_esp + 1);
      unsigned pos = *(unsigned *)(user_esp + 2);
      seek (fd, pos);
      break;
    }

    case SYS_TELL: {
      check_user_pointer_range(user_esp + 1, sizeof(int));

      int fd = *(int *)(user_esp + 1);
      f->eax = tell (fd);
      break;
    }

    case SYS_CLOSE: {
      check_user_pointer_range(user_esp + 1, sizeof(int));
      
      int fd = *(int *)(user_esp + 1);
      close (fd);
      break;
    }

    case SYS_MMAP: {
      check_user_pointer_range(user_esp + 1, sizeof(int));
      check_user_pointer_range(user_esp + 2, sizeof(void *));
      int fd = *(int *)(user_esp + 1);
      void *addr = *(void **)(user_esp + 2);
      f->eax = mmap (fd, addr);
      break;
    }
    case SYS_MUNMAP: {
      check_user_pointer_range(user_esp + 1, sizeof(int));
      mapid_t mapping = *(int *)(user_esp + 1);
      munmap (mapping);
      break;
    }
    default:
      exit (-1);
      break;
  }
}