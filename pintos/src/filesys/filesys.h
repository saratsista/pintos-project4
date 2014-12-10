#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */
#define DEFAULT_DIR_SIZE 2  /* Number of entries in directory when
				  created intially -- for . and .. */

/* Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

bool dirsys_create (const char *name);
struct dir *filesys_parent_dir (const char *name, char **file_name);
char *filesys_get_absolute_path (const char *rel_path);

#endif /* filesys/filesys.h */
