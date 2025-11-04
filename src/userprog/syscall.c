#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


#include "userprog/pagedir.h"
#include "threads/vaddr.h"

// functions to be run each time we use systemcall to verify the validity of user pointers
void check_user_pointer(const void *uaddr) {
    if (uaddr == NULL || !is_user_vaddr(uaddr)) {
        exit(-1);  // terminate process if pointer is invalid
    }
}
void write_user_buffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        check_user_pointer(buffer + i);  // check each byte
        char c = buffer[i];              // safe to read
        putbuf(&c, 1);                   // example: write to console
    }
}

void halt(void) {
    shutdown_power_off();  // Pintos function to stop the machine
}

void exit(int status) {
    struct thread *cur = thread_current();
    printf("%s: exit(%d)\n", cur->name, status);  // optional logging
    thread_exit();  // terminates the process
}

void write(int fd, const char *buffer, unsigned size) {
    if (fd == 0){return -1;}
    if (fd == 1) {  // stdout
        putbuf(buffer, size);
    } else {
        // For now, you can ignore file descriptors other than stdout
    }
}

pid_t exec (const char *cmd_line){
    pid_t pid = process_execute(cmd_line);
    if(pid == TID_ERROR){
        return -1;    
    }
    else {
        return pid;
    }
}

int read (int fd, void *buffer, unsigned size){
    if (fd == 1){return -1;}
    lock_acquire(&filesys_lock);
    int bytes_read = 0;
    else if (fd == 0){
        for (unsigned i = 0; i < size; i++){
            ((uint8_t *) buffer)[i] = input_getc();
            bytes_read++;
        }
    } else {
        // NOT implemented yet
        struct file *f = thread_current()->fd_table[fd];
        if(f){
            bytes_read = file_read(f, buffer, size);
        }
    }
    lock_release(&filesys_lock);
    return bytes_read;
}

int filesize(int fd){
    if (fd == 1 || fd == 0){return -1;}
    // NOT implemented yet
    struct file *f = thread_current()->fd_table[fd];
    if(f){
        return file_length(f);
    }
    return -1;
}

void seek (int fd, unsigned position){
    if (fd == 1 || fd == 0){return;}
    // NOT implemented yet
    struct file *f = thread_current()->fd_table[fd];
    if(f == NULL){
        return;
    }
    file_seek(f, position);
}

unsigned tell (int fd){
    if (fd == 1 || fd == 0){return -1;}
    // NOT implemented yet
    struct file *f = thread_current()->fd_table[fd];
    if(f == NULL){
        return -1;
    }
    return file_tell(f);
}

void close (int fd){
    if (fd == 1 || fd == 0){return;}
    // NOT implemented yet
    struct file *f = thread_current()->fd_table[fd];
    if(f == NULL){
        return;
    }
    file_close(f);
    thread_current()->fd_table[fd] = NULL;
}


static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // esp points to the user stack at the time of the syscall
  uint32_t *user_esp = f->esp;

  check_user_pointer(user_esp);
  int syscall_number = *user_esp;

  // Step 3: dispatch to handler
  switch (syscall_number) {
      case SYS:
      {}
      case SYS_HALT:
      {
          halt();
          break;
      }
      case SYS_EXIT:
      {
          int status = *(int *)(user_esp + 1); // argument from user stack
          exit(status);
          break;
      }
      case SYS_EXEC:
      {
        const char *cmd_line = *(const char **)(user_esp + 1);
        check_user_pointer(cmd_line); // Maybe check the string as well
        f->eax = exec(cmd_line);
        break;
      }
      case SYS_WRITE:
      {
          int fd = *(int *)(user_esp + 1);
          char *buffer = *(char **)(user_esp + 2);
          unsigned size = *(unsigned *)(user_esp + 3);

          check_user_pointer(buffer); // make sure buffer is valid
          write(fd, buffer, size);
          break;
      }
      default:
          printf("Unknown syscall %d\n", syscall_number);
          exit(-1);
          break;
  }
  thread_exit (); // Problem
}
