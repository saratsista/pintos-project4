#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/off_t.h"

#define BUFFER_CACHE_SIZE 64

/* Buffer Cache: List of 64 cache entries */
static struct list buffer_cache;

/* Lock acquired while evicting an entry */
struct lock evict_lock;

/* Each cache entry in the buffer_cache */
struct cache_entry
{
  char data[BLOCK_SECTOR_SIZE];	 /* Data block in each cache_entry */
  block_sector_t sector;	 /* On-disk sector number for the entry */
  bool accessed;
  bool dirty;			 /* Dirty bit for cache_entry */
  int open_count;		 /* Number of threads currently accessing 
				    the entry */
  off_t  bytes_covered;		 /* Number of bytes covered by this sector
				    from the beginning of the file */ 
  struct lock update_lock	 /* lock acquired when growing the sector
     				    to set is_growing */
  bool is_growing;		 /* Boolean to indicate that file is growing */
  struct list_elem elem;
};

void init_buffer_cache (void);
void read_cache (block_sector_t, void *);
void write_cache (block_sector_t, off_t, void *);
struct cache_entry* allocate_cache_entry (block_sector_t, off_t);

#endif /* filesys/cache.h */
