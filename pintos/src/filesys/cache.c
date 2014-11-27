#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"

void
init_buffer_cache ()
{
  lock_init (&cache_lock);
  list_init (&buffer_cache);

  int i;
  lock_acquire (&cache_lock);
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
   {
     struct cache_entry *entry = allocate_cache_entry ();
     list_push_front (&buffer_cache, &entry->elem);
   }  
  lock_release (&cache_lock);
}

void
write_cache (block_sector_t sector, int valid_bytes, void *buffer)
{
  struct cache_entry *entry = find_cache_entry (sector, false); 
  if (!entry)
   {
     entry = find_cache_entry (sector, true);
     if (!entry)
      PANIC ("Disk Full");
   }
 
  memcpy (entry->data, buffer, valid_bytes);
  entry->sector = sector;
  entry->valid_bytes = valid_bytes;
  entry->dirty = true;
}

void
read_cache (block_sector_t sector, void *buffer)
{
  int zero_bytes;
  struct cache_entry *entry = find_cache_entry (sector, false);
  if (!entry)
     entry = find_cache_entry (sector, true);

  if (entry)
   {
     memcpy (buffer, entry->data, entry->valid_bytes);
     zero_bytes = BLOCK_SECTOR_SIZE - entry->valid_bytes;
     if (zero_bytes != 0)
      {
        memset (buffer + entry->valid_bytes, 0, zero_bytes);
      }
   }
  else
   {
     PANIC ("Disk Full!");
   }     
}

struct cache_entry *
allocate_cache_entry ()
{
    struct cache_entry *entry = NULL;

     entry = calloc (1, sizeof (struct cache_entry));
     entry->sector = EMPTY;
     entry->accessed = false;
     entry->dirty = false;
     entry->valid_bytes = EMPTY;
     lock_init (&entry->update_lock);
     entry->is_growing = false;
     return entry;
}

/* Looks for a cache_entry in buffer_cache based on sector number.
   If found, removes and pushes it to front of the list.
   @param: sector_id - sector number of needed cache_entry
           unused    - Do we need to find an unused cache_entry? */
struct cache_entry *
find_cache_entry (block_sector_t sector_id, bool unused)
{
  struct cache_entry *entry = NULL;
  struct list_elem *e;
  block_sector_t sector = (unused == true)? EMPTY:sector_id;

  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
       e = list_next (e))
   {
     entry = list_entry (e, struct cache_entry, elem);
     if (entry->sector == (block_sector_t)sector)
      {
       list_remove (e);
       list_push_front (&buffer_cache, &entry->elem);
       return entry;
      }
   }
   /* If an unused sector is requested and is not present in cache,
      evict a cache_entry and return it */
   if (sector == EMPTY)
    {
      lock_acquire (&cache_lock);
      evict_cache_entry ();
      entry = allocate_cache_entry ();
      list_push_front (&buffer_cache, &entry->elem);
      lock_release (&cache_lock);
      return entry; 
    } 
  return NULL;   
}

bool
evict_cache_entry ()
{
  struct list_elem *e = list_end (&buffer_cache);
  struct cache_entry *entry = list_entry (e, struct cache_entry, elem);

  if (entry->dirty)
   {
     block_write (fs_device, entry->sector, entry->data);
   }
  list_remove (e);
  free (entry);
  return true;
}
