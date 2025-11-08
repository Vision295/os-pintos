#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

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

static void
check_user_pointer_range(const void *uaddr, size_t size) {
    struct thread *t = thread_current();
    
    // Handle empty buffer case
    if (size == 0) {
        return;
    }
    
    const void *start = uaddr;
    const void *end = (const char *)uaddr + size - 1;
    // Check 1: NULL check and verify both start and end are in user space
    if (start == NULL || !is_user_vaddr(start) || !is_user_vaddr(end)) {
        exit(-1);
    }
    // Check 2: Iterate over pages within the range
    const void *current_ptr = pg_round_down(start); // Start at page boundary
    const void *end_page = pg_round_down(end);       // End page boundary
    
    while (current_ptr <= end_page) {
        // Check if the page is mapped in the process's page directory
        if (pagedir_get_page(t->pagedir, current_ptr) == NULL) {
            exit(-1);
        }
        
        // Move to the next page
        current_ptr = (const char *)current_ptr + PGSIZE;
    }
}

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
  printf("Made it here2\n");
  //cur->child_info->exit_status = status;
  // DEBUG
  if (cur->child_info) {
    cur->child_info->exit_status = status;
    cur->child_info->has_exited = true;
    sema_up (&cur->child_info->wait_sema);
  }

  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

/* Start another process. */
pid_t
exec (const char *cmd_line) {
  check_user_pointer (cmd_line);

  pid_t pid = process_execute (cmd_line);
  if (pid == TID_ERROR)
    return -1;

  struct child_info *info = thread_find_child (pid);
  if (!info)
    return -1;

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

  // DEBUG  
  // struct thread *cur = thread_current();
  // if(cur->executable != NULL){
  //   if(file_get_inode(f) == file_get_inode(cur->executable)){
  //     file_deny_write(f);
  //   }
  // }
  int fd = thread_get_fd ();
  if (fd == -1) {
    file_close (f);
    return -1;
  }

  thread_current ()->fd_table[fd] = f;
  return fd;
}

int
filesize (int fd) {
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
    for (unsigned i = 0; i < size; i++)
      ((uint8_t *) buffer)[i] = input_getc ();
    bytes_read = size;
  } else if (fd > 1 && fd < MAX_FD) {
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

  if (fd == 1) { /* stdout */
    putbuf (buffer, size);
    bytes_written = size;
  } else if (fd > 1 && fd < MAX_FD) {
    struct file *f = thread_current ()->fd_table[fd];
    if (f)
      bytes_written = file_write (f, buffer, size);
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

/* System call initialization. */
void
syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

/* System call handler - FIXED VERSION for exec-bound-2 */
static void
syscall_handler (struct intr_frame *f) {
  uint32_t *user_esp = f->esp;
  
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
      /* CRITICAL FIX: Validate all 4 bytes of the pointer argument */
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
      f->eax = read (fd, buffer, size);
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

    default:
      exit (-1);
      break;
  }
}