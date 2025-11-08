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
    const void *start = uaddr;
    // Calculate the last byte of the buffer. 
    // If size is 0, this check will be skipped, which is correct.
    const void *end = (const char *)uaddr + size - 1; 

    // The current address pointer for iteration, starting at the beginning
    const void *current_ptr = start;

    // Check 1: Simple V-Address and NULL check for the entire range
    if (!is_user_vaddr(start) || !is_user_vaddr(end) || start == NULL) {
        exit(-1);
    }

    // Check 2: Iterate over pages within the range
    while (current_ptr <= end) {
        // 1. Check if the address is above the kernel boundary (already done above, but safe)
        if (!is_user_vaddr(current_ptr)) {
            exit(-1);
        }

        // 2. Check if the page is mapped in the process's page directory
        if (pagedir_get_page(t->pagedir, current_ptr) == NULL) {
            exit(-1);
        }

        // Move to the beginning of the *next* page. 
        // If current_ptr is already at a page boundary, 
        // PGSIZE will move it to the next one.
        // This is much faster than checking byte-by-byte.
        current_ptr = pg_round_down(current_ptr) + PGSIZE;
    }
}

static void check_user_string(const char *str) {
    while (true) {
        check_user_pointer(str); // check this byte
        if (*str == '\0') break; // reached end of string
        str++;
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

/* System call handler. */
static void
syscall_handler (struct intr_frame *f) {
  uint32_t *user_esp = f->esp;
  
  /* First, validate the stack pointer itself */
  check_user_pointer (user_esp);
  
  /* Validate we can read the syscall number (a 4-byte int) */
  check_user_pointer_range(user_esp, sizeof(int));

  int syscall_number = *user_esp;

  switch (syscall_number) {
    case SYS_HALT:
      halt ();
      break;

    case SYS_EXIT: {
      check_user_pointer (user_esp + 1);
      int status = *(int *)(user_esp + 1);
      exit (status);
      break;
    }

    case SYS_EXEC: {
      check_user_pointer (user_esp + 1);
      const char *cmd_line = *(const char **)(user_esp + 1);
      check_user_string (cmd_line);
      f->eax = exec (cmd_line);
      break;
    }

    case SYS_WAIT: {
      check_user_pointer (user_esp + 1);
      pid_t pid = *(pid_t *)(user_esp + 1);
      f->eax = wait (pid);
      break;
    }

    case SYS_CREATE: {
      check_user_pointer (user_esp + 1);
      check_user_pointer (user_esp + 2);
      const char *file = *(const char **)(user_esp + 1);
      check_user_string (file);
      unsigned initial_size = *(unsigned *)(user_esp + 2);
      f->eax = create (file, initial_size);
      break;
    }

    case SYS_REMOVE: {
      check_user_pointer (user_esp + 1);
      const char *file = *(const char **)(user_esp + 1);
      check_user_string (file);
      f->eax = remove (file);
      break;
    }

    case SYS_OPEN: {
      check_user_pointer (user_esp + 1);
      const char *file = *(const char **)(user_esp + 1);
      check_user_string (file);
      f->eax = open (file);
      break;
    }

    case SYS_FILESIZE: {
      check_user_pointer (user_esp + 1);
      int fd = *(int *)(user_esp + 1);
      f->eax = filesize (fd);
      break;
    }

    case SYS_READ: {
      check_user_pointer_range(user_esp + 1, sizeof(int));
      check_user_pointer_range(user_esp + 2, sizeof(void *));
      check_user_pointer_range(user_esp + 3, sizeof(unsigned));

      /* Now it's safe to read the arguments */
      int fd = *(int *)(user_esp + 1);
      void *buffer = *(void **)(user_esp + 2);
      unsigned size = *(unsigned *)(user_esp + 3);
      
      f->eax = read (fd, buffer, size);
      break;
    }

    case SYS_WRITE: {
      check_user_pointer_range(user_esp + 1, sizeof(int));
      check_user_pointer_range(user_esp + 2, sizeof(void *));
      check_user_pointer_range(user_esp + 3, sizeof(unsigned));

      int fd = *(int *)(user_esp + 1);
      const void *buffer = *(const void **)(user_esp + 2);
      unsigned size = *(unsigned *)(user_esp + 3);
      
      /* Validate the buffer pointer - this is where bad-write2 test fails */
      check_user_pointer (buffer);
      
      f->eax = write (fd, buffer, size);
      break;
    }

    case SYS_SEEK: {
      check_user_pointer (user_esp + 1);
      check_user_pointer (user_esp + 2);
      int fd = *(int *)(user_esp + 1);
      unsigned pos = *(unsigned *)(user_esp + 2);
      seek (fd, pos);
      break;
    }

    case SYS_TELL: {
      check_user_pointer (user_esp + 1);
      int fd = *(int *)(user_esp + 1);
      f->eax = tell (fd);
      break;
    }

    case SYS_CLOSE: {
      check_user_pointer (user_esp + 1);
      int fd = *(int *)(user_esp + 1);
      close (fd);
      break;
    }

    default:
      exit (-1);
      break;
  }
}