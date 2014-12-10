#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  buffer_cache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  buffer_cache_flush ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *_name, off_t initial_size) 
{
  if (thread_current ()->cwd_deleted == true)
    return false;

  block_sector_t inode_sector = 0;
  char *file_name; 
  char *name = calloc (1, strlen (_name)+1);
  strlcpy (name, _name, strlen (_name)+1);
  struct dir *dir = filesys_parent_dir (name, &file_name);
  bool success;

  if (dir == NULL)
   {
     success = false;
     goto done;
   }

  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, file_name, inode_sector));

done:
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free (name);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *_name)
{
  char *file_name;
  struct thread *t = thread_current ();

  if (t->cwd_deleted == true)
    return NULL;

  if (strcmp (_name, "/") == 0)
    {
      return (struct file *)dir_open_root ();
    }

  char *name = calloc (1, MAX_PATH + 1);
  if (strcmp (_name, ".") == 0)
    strlcpy (name, t->cwd, strlen (t->cwd));
  else
    strlcpy (name, _name, MAX_PATH + 1);


  struct dir *dir = filesys_parent_dir (name, &file_name); 
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);
  dir_close (dir);

  if (inode == NULL)
   return NULL;

  free (name);
  /* Open directory/file */
  if (inode_is_directory (inode))
    return (struct file *)dir_open (inode);
  else
    return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *_name) 
{
  char *file_name;
  struct file *file = filesys_open (_name);

  if (file != NULL && 
      inode_is_directory (file_get_inode (file)))
    if (!dir_empty ((struct dir *)file))
      return false;

  char *name = calloc (1, strlen (_name)+1);
  strlcpy (name, _name, strlen (_name)+1);
  struct dir *dir = filesys_parent_dir (name, &file_name);
  bool success = dir != NULL && dir_remove (dir, file_name);

  if (strcmp (_name, thread_current ()->cwd) == 0)
    thread_current ()->cwd_deleted = true;

  dir_close (dir); 
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, NULL))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Creates a directory named NAME and returns TRUE if success and
   FALSE otherwise.  */
bool
dirsys_create (const char *name)
{
  block_sector_t inode_sector = 0;
  char *file_name;
  struct dir *dir = filesys_parent_dir (name, &file_name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, DEFAULT_DIR_SIZE, name)
                  && dir_add (dir, file_name, inode_sector));

  if (!success && inode_sector != 0) 
     free_map_release (inode_sector, 1);
   
  dir_close (dir);
  return success;
}

/* Returns the parent directory of the leaf node in path NAME 
   Returns NULL if any directory in the path is invalid or
		if any directory in path has name > NAME_MAX + 1 or
		if any directory in the path is not a child of parent.
   Stores the name of the leaf in the FILE_NAME.
   Accepts both absolute and relative paths.  */
struct dir *
filesys_parent_dir (const char *path, char **file_name)
{
  struct thread *cur = thread_current ();
  char *save_ptr, *token;
  struct inode *inode = NULL;
  struct dir *start, *next;

  /* Assign start to cwd and check if NAME has no '/'.
     If so, return start. */
  start = dir_open (inode_open (cur->cwd_sector)); 
  *file_name = NULL;
  if (strchr (path, '/') == NULL)
   {
    if (strlen (path) > NAME_MAX + 1)
      return NULL;
    *file_name = (char *)path;
    return start;
   }
 
  if (*path == '/')
   start = dir_open_root ();

  token = strtok_r ((char *)path, "/", &save_ptr);
  if ((strchr (token , '/') == NULL && 
       (strcmp (save_ptr, "") == 0)))
   {
     *file_name = token;
      return dir_open_root ();
   }
  
  while ((*file_name = strtok_r (NULL, "/", &save_ptr)) != NULL)
   {
    if (strlen (token) > NAME_MAX + 1)
       return NULL;

     if (dir_lookup (start, token, &inode))
      {
       next = dir_open (inode);	
       start = next;
      }
     else
      {
        return NULL;
      }
   token = *file_name;
  }        
  *file_name = token;
  return start;
}

/* Returns the absolute_path given a relative path.
   The caller is responsible for freeing the returned string.
   Returns NULL on failure.  */
char *
filesys_get_absolute_path (const char *_rel_path)
{
  if (_rel_path == NULL)
    return NULL;

  char *abs_path = calloc (1, MAX_PATH);
  char *rel_path = (char *)_rel_path;
  struct thread *t = thread_current ();
  char *save_ptr, *token;
  
  if (strcmp (rel_path, "/") == 0)
    return rel_path;

  if (*rel_path == '/')
   {
    strlcpy (abs_path, "/", MAX_PATH);
   }
  else
   {
     strlcpy (abs_path, t->cwd, MAX_PATH);
   }

  token = strtok_r (rel_path, "/", &save_ptr);
  while (token != NULL) 
   {
     if (strcmp (token, "..") == 0)
      {
        *(strrchr (abs_path, '/')) ='\0';
      }  
     else if(*token != '.')
       {
         strlcat (abs_path, token, MAX_PATH);
         strlcat (abs_path, "/", MAX_PATH);
       }
      if (strcmp (abs_path, "") == 0)
         strlcat (abs_path, "/", MAX_PATH);
  
      token = strtok_r (NULL, "/", &save_ptr);
    } 
   
     return abs_path;
}
