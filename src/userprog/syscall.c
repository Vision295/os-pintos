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
    const uint8_t *ptr = uaddr;

    for (size_t i = 0; i < size; i++) {
        if (ptr + i == NULL || !is_user_vaddr(ptr + i) ||
            pagedir_get_page(t->pagedir, ptr + i) == NULL) {
            exit(-1); // terminate process safely
        }
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
  check_user_pointer (user_esp);
  check_user_pointer_range(user_esp, sizeof(int));         // syscall number
  check_user_pointer_range(f->esp + 4, sizeof(char*)); // exec pointer


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
      unsigned initial_size = *(unsigned *)(user_esp + 2);
      f->eax = create (file, initial_size);
      break;
    }

    case SYS_REMOVE: {
      check_user_pointer (user_esp + 1);
      const char *file = *(const char **)(user_esp + 1);
      f->eax = remove (file);
      break;
    }

    case SYS_OPEN: {
      check_user_pointer (user_esp + 1);
      const char *file = *(const char **)(user_esp + 1);
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
      check_user_pointer (user_esp + 1);
      check_user_pointer (user_esp + 2);
      check_user_pointer (user_esp + 3);
      int fd = *(int *)(user_esp + 1);
      void *buffer = *(void **)(user_esp + 2);
      unsigned size = *(unsigned *)(user_esp + 3);
      check_user_pointer (buffer);
      f->eax = read (fd, buffer, size);
      break;
    }

    case SYS_WRITE: {
      check_user_pointer (user_esp + 1);
      check_user_pointer (user_esp + 2);
      check_user_pointer (user_esp + 3);
      int fd = *(int *)(user_esp + 1);
      const void *buffer = *(const void **)(user_esp + 2);
      unsigned size = *(unsigned *)(user_esp + 3);
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
