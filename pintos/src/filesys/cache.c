#include "filesys/cache.h"

/* Current size of the buffer cache */
int static cur_cache_size;

void
init_buffer_cache ()
{
  init_lock (&evict_lock);
  init_list (&buffer_cache);
}

struct cache_entry *
allocate_cache_entry (block_sector_t sector, off_t bytes_covered)
{
  if (cur_cache_size == BUFFER_CACHE_SIZE)
   {
     PANIC ("Buffer Cache Full!");
   }
  else
   {
     struct cache_entry *entry = calloc (1, sizeof (struct cache_entry));
     entry->sector = sector;
     entry->accessed = false;
     entry->dirty = false;
     entry->bytes_covered = bytes_covered;
     lock_init (&entry->update_lock);
     entry->is_growing = false;
   }
}

