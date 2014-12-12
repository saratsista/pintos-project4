#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include <limits.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/off_t.h"

#define BUFFER_CACHE_SIZE 64
#define EMPTY UINT_MAX 

/* Buffer Cache: List of 64 cache entries */
static struct list buffer_cache;

/* Lock acquired while evicting an entry */
struct lock cache_lock;

/* Each cache entry in the buffer_cache */
struct cache_entry
{
  char data[BLOCK_SECTOR_SIZE]; /* Data block in each cache_entry */
  block_sector_t sector;	 /* On-disk sector number for the entry */
  bool accessed;
  bool dirty;			 /* Dirty bit for cache_entry */
  int open_count;		 /* Number of threads currently accessing 
				    the entry */
  int  valid_bytes;		 /* Number of valid bytes in this sector*/
  struct lock update_lock;	 /* lock acquired when growing the sector
     				    to set is_growing */
  bool is_growing;		 /* Boolean to indicate that file is growing */
  struct list_elem elem;
};

void buffer_cache_init (void);
struct cache_entry *cache_read (block_sector_t, int);
void cache_write (block_sector_t, void *, int);
struct cache_entry* allocate_cache_entry (void);
struct cache_entry* find_cache_entry (block_sector_t, bool);
void evict_cache_entry (void);
void buffer_cache_flush (void);

#endif /* filesys/cache.h */
