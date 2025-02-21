#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include <string.h>
#include <ctype.h>
#include <devices/shutdown.h>
#include <devices/input.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include <kernel/console.h>
#include <filesys/filesys.h>
#include <filesys/file.h>
#include <filesys/inode.h>

#define MAX_ARGS 3

static void syscall_handler (struct intr_frame *);
void validate_pointer (void *ptr);
void get_arguments (int *esp, int *args, int count);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int args[MAX_ARGS];
  validate_pointer (f->esp);
  int *sp = (int *)f->esp;
  struct thread *cur = thread_current ();

  switch (*sp)
  {
   case SYS_HALT:
      shutdown_power_off ();

   case SYS_EXIT:
      get_arguments (sp, &args[0], 1); 
      exit ((int)args[0]);
      break;

   case SYS_EXEC:
      get_arguments (sp, &args[0], 1);
      f->eax = exec ((const char *)pagedir_get_page (cur->pagedir,
					(const void *) args[0]));
      break;

   case SYS_WAIT:
      get_arguments (sp, &args[0], 1);
      f->eax = wait ((pid_t)args[0]);
      break;
    
   case SYS_WRITE:
      get_arguments (sp, &args[0], 3);
      args[1] = (int)pagedir_get_page (cur->pagedir, (const void *)args[1]);
      f->eax = write ((int)args[0], (void *)args[1], (unsigned)args[2]);
      break;
    
   case SYS_READ:
      get_arguments (sp, &args[0], 3);
      f->eax = read ((int)args[0], (void *)args[1], (unsigned)args[2]);
      break;

   case SYS_CREATE:
      get_arguments (sp, &args[0], 2);
      args[0] =(int) pagedir_get_page (cur->pagedir, (const void *)args[0]);
      f->eax = create ((char *)args[0], (unsigned) args[1]);
      break;

    case SYS_REMOVE:
       get_arguments (sp, &args[0], 1);
       char *file_to_close = (char *)pagedir_get_page (cur->pagedir,
					(const void *)args[0]);
       f->eax = remove (file_to_close);
       break;

    case SYS_OPEN:
       get_arguments (sp, &args[0], 1);
       f->eax = open ((char *)args[0]);
       break; 
  
    case SYS_FILESIZE:
       get_arguments (sp, &args[0], 1);
       f->eax = filesize ((int)args[0]);
       break;

    case SYS_CLOSE:
       get_arguments (sp, &args[0], 1);
       args[0] = (int)pagedir_get_page (cur->pagedir, (const void *)args[0]);
       close ((int)args[0]);
       break;       

    case SYS_TELL:
       get_arguments (sp, &args[0], 1);
       f->eax = tell ((int)args[0]);
       break;

    case SYS_SEEK:
       get_arguments (sp, &args[0], 2);
       seek ((int)args[0], (unsigned)args[1]);
       break; 

    case SYS_MKDIR:
       get_arguments (sp, &args[0], 1);
       f->eax = mkdir ((const char*)args[0]);
       break;

    case SYS_CHDIR:
       get_arguments (sp, &args[0], 1);
       f->eax = chdir ((const char*)args[0]);
       break; 

    case SYS_ISDIR:
       get_arguments (sp, &args[0], 1);
       f->eax = isdir ((int)args[0]);
       break;

    case SYS_INUMBER:
       get_arguments (sp, &args[0], 1);
       f->eax = inumber ((int)args[0]);
       break;

    case SYS_READDIR:
        get_arguments (sp, &args[0], 2);
        f->eax = readdir ((int)args[0], (char *)args[1]);
	break; 
  }
}

void
get_arguments (int *esp, int *args, int count)
{
  int i;
  for (i = 0; i < count; i++)
  {
    int *next = ((esp + i) + 1);
    validate_pointer (next);
    args[i] = *next;
  }
}

void
validate_pointer (void *ptr)
{
  if (!is_user_vaddr (ptr)) 
    exit (-1);
  if  ((pagedir_get_page (thread_current ()->pagedir, ptr) == NULL))
    exit (-1);
}

void
exit (int status)
{
  struct thread *cur = thread_current ();
  cur->md->exit_status = status;
  sema_up (&cur->md->completed);
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

bool
create (const char *file_name, unsigned size)
{
  int return_value;
  if (file_name == NULL)
    exit (-1);    
  return_value = filesys_create (file_name, size);
  return return_value;
}

int
open (const char *file)
{
  struct thread *cur = thread_current ();
  validate_pointer ((void *)file);
  if (file == NULL)
    exit (-1);
  if (strcmp (file, "") == 0)
    return -1;
  struct file *open_file = filesys_open (file);
  if (open_file == NULL)
    return -1;
  if ((cur->md->exec_file != NULL) &&
      file_get_inode (open_file) == file_get_inode(cur->md->exec_file))
      file_deny_write (open_file);
  struct file **fd_array = cur->fd;
  int k;
  for (k = 2; k < MAX_FD; k++)
  { 
    if (fd_array[k] == NULL)
    {
     fd_array[k] = open_file;
     break;
    }
  }
   return k;
}  

int
read (int fd, void *_buffer, unsigned size)
{
  struct thread *cur = thread_current ();
  char *buffer = (char *)_buffer;
  validate_pointer (buffer);
  int retval = -1;
  if (fd == 1 || fd < 0 || fd > MAX_FD)
    exit (-1); 
  if (fd == 0)
  {
    char c;
    unsigned i = 0;
    while ((c = input_getc ())!= '\n')
    {
      buffer[i] = c; 
      i++;
      if (i == size-1) break;
    }
  }
  else {
    struct file *file = cur->fd[fd];
    if (file != NULL) {
      if ((cur->md->exec_file != NULL &&
	   file_get_inode (file) == file_get_inode(cur->md->exec_file)))
        file_deny_write (file);
      retval = file_read (file, buffer, size);
      cur->fd[fd] = file;
    }
    else retval = -1;
  }
  return retval;
}

int
write (int file_desc, const void *_buffer, unsigned size)
{
  char *buffer = (char *)_buffer;
  struct thread *cur = thread_current ();
  struct file *file_to_write;

  if (buffer == NULL)
    exit (-1);
  int retval;
  if (file_desc < 1 || file_desc > MAX_FD)
    return -1;
  if (file_desc == 1) {
    putbuf (buffer, size);
    retval = size;
  }
  else
  {
    file_to_write = cur->fd[file_desc];
    if (file_to_write != NULL)
    {
        /* If directory, fail */
        if (inode_is_directory (file_get_inode (file_to_write)))
          return -1;
    	retval = file_write (file_to_write, buffer, size);
    	cur->fd[file_desc] = file_to_write;
        file_allow_write (file_to_write);
    }
    else retval = -1;
  }
  return retval;
}

void
close (int fd)
{
  struct thread *cur = thread_current ();
  file_close (cur->fd[fd]);
  cur->fd[fd] = NULL;
}

int
filesize (int fd)
{
  struct file *file = thread_current ()->fd[fd];
  if (file == NULL)
   exit (-1);
  return file_length (file);
}

unsigned
tell (int fd)
{
  struct file *file = thread_current ()->fd[fd];
  if (file == NULL)
   exit (-1);
  return file_tell (file);
} 

void
seek (int fd, unsigned position)
{
  struct file *file = thread_current ()->fd[fd];
  if (file == NULL)
   exit (-1);
  file_seek (file, position);
}

pid_t
exec (const char *file)
{
  if (file == NULL)
   exit (-1);
  tid_t child_tid = process_execute (file);
  return (pid_t)child_tid;
}

int
wait (pid_t pid)
{
  return process_wait((tid_t)pid);
}

bool
mkdir (const char *dir)
{
  return dirsys_create (dir);   
}

bool
chdir (const char *dir)
{
  if (strlen (dir) > MAX_PATH)
   return false;
  
  struct thread *cur = thread_current ();
  char *abs_path, *file_name;
  struct inode *inode;

  abs_path = filesys_get_absolute_path (dir);
  struct dir *parent = filesys_parent_dir (dir, &file_name);
  if (!dir_lookup (parent, file_name, &inode))
   return false;

  cur->cwd_sector = inode_get_inumber (inode);
 
  if (abs_path == NULL)
    return false;

  if (strlcpy (cur->cwd, abs_path, MAX_PATH + 1) != strlen (abs_path))
    return false;

  free (abs_path);
  return true;  
}

bool
remove (const char *file)
{
  if (strcmp (file, "/") == 0)
    return false;
  char *abs_path = filesys_get_absolute_path (file);
  return filesys_remove (abs_path);
}

bool
isdir (int fd)
{
  struct thread *t = thread_current ();
  if (t->fd[fd] == NULL)
   return false;
  struct inode *inode = file_get_inode (t->fd[fd]);
  return inode_is_directory (inode);
}

bool
readdir (int fd, char *name)
{
  struct thread *t = thread_current ();
  struct dir *dir = (struct dir *)t->fd[fd];
  bool success;
  
  if (!inode_is_directory (dir_get_inode (dir)))
    return false;

  while ((success = dir_readdir (dir, name)))
   {
     if ((strcmp (".", name) == 0) ||
	 (strcmp ("..", name) == 0))
        return false;
     else
       return success;
   }
  return success;
}

int
inumber (int fd)
{
  struct thread *t = thread_current ();
  struct inode *inode = file_get_inode (t->fd[fd]);
  return inode_get_inumber (inode);
}
