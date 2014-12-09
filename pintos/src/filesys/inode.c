#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define MAX_SECTOR_INDEX 128

static char zeros[BLOCK_SECTOR_SIZE];
static block_sector_t next_di;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t direct;               /* First data sector. */
    struct indirect indirect;
    struct d_indirect d_indirect;
    enum pointer pointer;
    off_t length;                       /* File size in bytes. */
    bool is_directory;			/* Is this file a directory? */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[118];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    block_sector_t direct;
    struct indirect indirect;
    struct d_indirect d_indirect;
    bool is_directory;
    off_t length;    			/* Length of file stored at this inode */
    struct lock growth_lock;
  };

/* Given a sector SECTOR and and index INDEX into the SECTOR,
   it returns the sector number stored at that INDEX.
   Used for calculating sector numbers in indirect and double indirect
   pointers */
static block_sector_t
sector_at_index (block_sector_t sector, off_t index)
{
  block_sector_t indirect_sector;
  block_sector_t *buffer = calloc (1, BLOCK_SECTOR_SIZE); 
  block_read (fs_device, sector, buffer);
  buffer += index;
  memcpy (&indirect_sector, buffer, sizeof (block_sector_t));
  return indirect_sector;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data at offset POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  if (pos > inode->length)
    return -1;
  
  off_t sector_index = pos / BLOCK_SECTOR_SIZE;
  
  if (sector_index == 0)
   {
     return inode->direct;
   }
  else if (sector_index > 0 && sector_index <= MAX_SECTOR_INDEX)
   {
     sector_index -= 1;
     return sector_at_index (inode->indirect.sector, sector_index);
   }
  else
   {
     sector_index -= (MAX_SECTOR_INDEX + 1);
     off_t d_indirect_index = sector_index / MAX_SECTOR_INDEX;
     block_sector_t tmp = sector_at_index (inode->d_indirect.sector,
					   d_indirect_index);
     off_t indirect_index = sector_index % MAX_SECTOR_INDEX;
    return sector_at_index (tmp, indirect_index);
   }
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool directory)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      int rem_sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->pointer = NONE;
      disk_inode->is_directory = directory;

     /* If length is zero, just create the inode and return */
     if (rem_sectors == 0)
	{
	  success = true;
          goto done;
	}

      /* Allocate direct pointer */
      success = free_map_allocate (1, &disk_inode->direct);
      if (!success)
         goto done;
      block_write (fs_device, disk_inode->direct, zeros);
      rem_sectors--;
      if (rem_sectors == 0)
       {
         disk_inode->pointer = DIRECT;
         goto done;
       }

      /* Allocate indirect pointer */
      if(!free_map_allocate (1, &disk_inode->indirect.sector))
         goto done;
      rem_sectors = inode_allocate_indirect (&disk_inode->indirect, rem_sectors);
      if (rem_sectors == -1)
        {
	  success = false;
	  goto done;
	}
      else if (rem_sectors == 0)
	{
          disk_inode->pointer = INDIRECT;
          goto done;
        }
     
       /* Allocate Double indirect pointer */
       if(!free_map_allocate (1, &disk_inode->d_indirect.sector))
         goto done; 
       rem_sectors = inode_allocate_double_indirect
		                  (&disk_inode->d_indirect, rem_sectors);
      if (rem_sectors == -1)
        {
	  success = false;
	  goto done;
	}
      else if (rem_sectors == 0)
	{
          disk_inode->pointer = DOUBLE_INDIRECT;
          goto done;
        }
    }

done:
  if (success)
    {
     block_write (fs_device, sector, disk_inode);  
    }
  free (disk_inode);
  return success;
}

/* Allocates sectors for indirect pointers for a file
   Returns -1 if allocation fails. 
   Else, returns the number of sectors remaining that
   need to be allocated.  */
int
inode_allocate_indirect (struct indirect *indirect, int sectors_left)
{
  block_sector_t *buffer = malloc (BLOCK_SECTOR_SIZE);

  block_read (fs_device, indirect->sector, buffer);
  while (indirect->offset < MAX_SECTOR_INDEX)
    {
      if (!free_map_allocate (1, (buffer + indirect->offset)))
       {
        free (buffer);
	return -1;
       }
      //printf (">>> sector allocated: %d\n",*(buffer+indirect->offset)); 
      block_write (fs_device, *(buffer + indirect->offset), zeros);
      sectors_left--;
      indirect->offset++;
      if (sectors_left == 0)
         break;
    }
  block_write (fs_device, indirect->sector, buffer);
  free (buffer);
  return sectors_left;
}

int 
inode_allocate_double_indirect (struct d_indirect *d_indirect,
				int sectors_left)
{
  int j;
  block_sector_t *buffer = malloc (BLOCK_SECTOR_SIZE);
  struct indirect indirect;

  block_read (fs_device, d_indirect->sector, buffer);
  for (j = d_indirect->off1; j < MAX_SECTOR_INDEX; j++)
   {
    if (next_di == 0 || ((block_sector_t)j == next_di)) {
    if (!free_map_allocate (1, (buffer + j)))
     {
       sectors_left = -1;
       goto done;
     }
     //printf (">>> %s: indirect sector allocated: %d\n", __func__,*(buffer+j)); 
    }
     indirect.sector = *(buffer + j);
     indirect.offset = d_indirect->off2;
     sectors_left = inode_allocate_indirect (&indirect, sectors_left);
     d_indirect->off1 = j;
     d_indirect->off2 = indirect.offset;
     next_di = j+1;
     if (sectors_left == -1)
        goto done;
     if (sectors_left == 0)
        break;
   }

done:
  if (sectors_left != -1)
     block_write (fs_device, d_indirect->sector, buffer);
  free (buffer);
  return sectors_left;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct inode_disk disk_inode; 
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  block_read (fs_device, sector, &disk_inode);
  inode->direct = disk_inode.direct;
  inode->indirect = disk_inode.indirect;
  inode->d_indirect = disk_inode.d_indirect;
  inode->length = disk_inode.length;
  inode->is_directory = disk_inode.is_directory;
  lock_init (&inode->growth_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Returns TRUE if INODE is a directory. FALSE otherwise */
bool
inode_is_directory (struct inode *inode)
{
  return inode->is_directory;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          inode_deallocate (inode);
        }

      free (inode); 
    }
}

void
inode_deallocate (struct inode *inode)
{
   block_sector_t sector, dsector;
   struct cache_entry *entry = NULL, *dentry = NULL;
   block_sector_t *dbuffer, *ibuffer;
   int i,j,k;

   /* Deallocate sector where inode is stored */
   free_map_release (inode->sector, 1);

   /* Deallocate direct pointer */
   free_map_release (inode->direct, 1);

   /* Deallocate indirect pointer */
   entry = cache_read (inode->indirect.sector, BLOCK_SECTOR_SIZE);
   ibuffer = (block_sector_t *)entry->data;
   for (i = 0; i < inode->indirect.offset; i++)
    {
     memcpy (&sector, ibuffer, sizeof (block_sector_t));
     free_map_release (sector, 1);
     ibuffer++; 
    }
    free_map_release (inode->indirect.sector, 1);

    /* Deallocate double indirect pointer */
    entry = cache_read (inode->d_indirect.sector, BLOCK_SECTOR_SIZE);
    ibuffer = (block_sector_t *)entry->data;
    for (j = 0; j < inode->d_indirect.off1; j++)
     {
      memcpy (&sector, ibuffer, sizeof (block_sector_t));
      dentry = cache_read (sector, BLOCK_SECTOR_SIZE);
      dbuffer = (block_sector_t *)dentry->data;
      for (k = 0; k < inode->d_indirect.off2; k++)
       {
         memcpy (&dsector, dbuffer, sizeof (block_sector_t));
         free_map_release (dsector, 1);
         dbuffer++;
       }
       free_map_release (sector, 1);
       ibuffer++;
     }
    free_map_release (inode->d_indirect.sector, 1);   

  if (entry)
    entry->sector = EMPTY;
  if (dentry)
    dentry->sector = EMPTY;
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  struct cache_entry *entry = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      //printf ("sector to read: %d\n", sector_idx);
      //printf ("offset to read: %d\n", offset);

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      entry = cache_read (sector_idx, BLOCK_SECTOR_SIZE);
      memcpy (buffer + bytes_read, entry->data + sector_ofs, chunk_size);
  
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  struct cache_entry *entry = NULL;

  if (inode->deny_write_cnt)
    return 0;
  //printf ("length of file before growth: %d\n", inode->length);

  /* If offset is greater than current length of the file
     grow the file */
  if ((size + offset) > inode->length)
   {
     lock_acquire (&inode->growth_lock);
     if (!grow_file (inode, offset + size))
      {
        lock_release (&inode->growth_lock);
        return 0;
      }
      lock_release (&inode->growth_lock);
   }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      //printf ("sector to write: %d\n", sector_idx);
      //printf ("offset to write: %d\n", offset);
      //printf ("length of file: %d\n", inode->length);

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      entry = cache_read (sector_idx, BLOCK_SECTOR_SIZE);
      memcpy (entry->data + sector_ofs, buffer + bytes_written, chunk_size);
      cache_write (sector_idx, entry->data, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}

/* Grows the INODE upto the given OFFSET */
bool
grow_file (struct inode *inode, off_t offset) 
{
  /* Check if the file is already grown */
  if (inode->length > offset)
   return true;  
  
  bool success = true;
  int bytes_left = (bytes_to_sectors(inode->length) * BLOCK_SECTOR_SIZE)
			- inode->length;
  int new_sectors = bytes_to_sectors (offset - inode->length - bytes_left);
  struct inode_disk *disk_inode = calloc (1, sizeof (struct inode_disk));

  block_read (fs_device, inode->sector, disk_inode);
  if (new_sectors == 0)
    goto done;
  //printf ("%s: new sectors = %d\n", __func__, new_sectors);
  //printf (">>> disk_inode->pointer = %d\n", disk_inode->pointer);
  //printf (">>> %s: offset = %d\n", __func__, offset);
  switch (disk_inode->pointer) 
   {
     case NONE:
         if (!free_map_allocate (1, &disk_inode->direct))
	  {
	    success = false;	
	    goto done;
          }
          block_write (fs_device, disk_inode->direct, zeros);
	  //printf (">>> Direct sector allocated: %d\n", disk_inode->direct);
          new_sectors--;
          disk_inode->pointer = DIRECT;
          if (new_sectors == 0)
            {
	     goto done;
            }
      case DIRECT:
           if (!free_map_allocate (1, &disk_inode->indirect.sector))
            {
	      success = false;
	      goto done;   
	    }
	    //printf (">>> indirect sector allocated: %d\n", disk_inode->indirect.sector);
            new_sectors = inode_allocate_indirect (&disk_inode->indirect, new_sectors);
            //printf ("Indirect pointer offset: %d\n", disk_inode->indirect.offset);
            if (new_sectors == -1)
             {
	 	 success = false;
	  	 goto done;
	     }
      	     else if (new_sectors >= 0 && new_sectors < MAX_SECTOR_INDEX)
	      {
          	disk_inode->pointer = INDIRECT;
		if (new_sectors == 0)
                  goto done;
              }
       case INDIRECT:
          if (disk_inode->indirect.offset < MAX_SECTOR_INDEX)
	   {
	     new_sectors = inode_allocate_indirect (&disk_inode->indirect, new_sectors);
            //printf ("%% Indirect pointer offset: %d\n", disk_inode->indirect.offset);
            if (new_sectors == -1)
             {
	 	 success = false;
	  	 goto done;
	     }
	   }
	  if (new_sectors > 0)//(disk_inode->indirect.offset == MAX_SECTOR_INDEX) 
	   {
	     if (!free_map_allocate (1, &disk_inode->d_indirect.sector))
	      {
		 success = false;
		 goto done;
	      }
	      //printf (">>> double indirect sector allocated: %d\n", disk_inode->d_indirect.sector);
	      disk_inode->pointer = DOUBLE_INDIRECT;
           }
      	   if (new_sectors == 0)
	    {
              goto done;
            }
       case DOUBLE_INDIRECT:
	   new_sectors = inode_allocate_double_indirect (&disk_inode->d_indirect,
							 new_sectors);
            if (new_sectors == -1)
             {
	 	 success = false;
	     }
	     goto done;
    } 
done:
   if (success)
    {
     /* Update disk_inode and write it to disk */
     disk_inode->length = offset;
     block_write (fs_device, inode->sector, disk_inode);  
     /* Update in-memory inode */ 
     inode->length = disk_inode->length;
     inode->direct = disk_inode->direct;
     inode->indirect = disk_inode->indirect;
     inode->d_indirect = disk_inode->d_indirect;
    }
     
   free (disk_inode);
   return success;
}
	   
