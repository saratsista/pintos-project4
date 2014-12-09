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
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = filesys_parent_dir (name);
  bool success;

  if (dir == NULL)
   {
     success = false;
     goto done;
   }

  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, name, inode_sector));

done:
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  if (inode == NULL)
   return NULL;

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
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
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

  struct dir *dir = filesys_parent_dir (name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, DEFAULT_DIR_SIZE, name)
                  && dir_add (dir, name, inode_sector));

  if (!success && inode_sector != 0) 
     free_map_release (inode_sector, 1);
   
  dir_close (dir);
  return success;
}

/* Returns the parent directory of the leaf file in path NAME 
   Returns NULL if any directory in the path is invalid or
		if any directory in path has name > NAME_MAX + 1 or
		if any directory in the path is not a child of parent.
*/
struct dir *
filesys_parent_dir (const char *name)
{
  struct thread *cur = thread_current ();
//  char *name = _name;
  char *save_ptr, *token;
  struct inode *inode = NULL;
  struct dir *start, *next;

  /* Assign start to cwd and check if NAME has no '/'.
     If so, return start. */
  start = dir_open (inode_open (cur->cwd_sector)); 
  if (strchr (name, '/') == NULL)
   {
    if (strlen (name) > NAME_MAX + 1)
      return NULL;
    return start;
   }

  /* Check if the first character in NAME is '\'. 
     If so, start is root */
  if (*name == '/')
    start = dir_open_root ();
  
  for (token = strtok_r ((char *)name, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
   {
     if (strlen (token) > NAME_MAX + 1)
       return NULL;
     if (dir_lookup (start, token, &inode))
      {
        next = dir_open (inode);	
	if (strchr (save_ptr, '/') == NULL)
          return next;
      }
     else
      {
        return NULL;
      }
    start = next;
   }        
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
  char *save_ptr;
  
  if (*rel_path == '/')
   {
    strlcpy (abs_path, rel_path, MAX_PATH);
    return abs_path;
   }

  if (*rel_path != '.')
     goto done;

  strtok_r (rel_path, "/", &save_ptr);
  rel_path = save_ptr;
         
   
done:
     strlcpy (abs_path, t->cwd, MAX_PATH);
     strlcat (abs_path, rel_path, MAX_PATH);
     return abs_path;
}
