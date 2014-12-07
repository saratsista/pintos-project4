#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "devices/timer.h"

#define FLUSH_FREQUENCY 50
thread_func write_behind_daemon;

void
buffer_cache_init ()
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

  /* Create the write_behind daemon */
  tid_t daemon_tid = thread_create ("write_behind_daemon", PRI_MAX - 1,
		     		     write_behind_daemon, NULL);    
  if (daemon_tid == TID_ERROR)
    PANIC ("Cannot create write-behind daemon!");
}

/* Function to write all dirty entries in buffer_cache to disk.
   Run when filesys_done() is called (during shutdown).
   Also run by the write behind daemon periodically to flush
   contents to disk */
void
buffer_cache_flush ()
{
  struct list_elem *e;
  struct cache_entry *entry;

  lock_acquire (&cache_lock);
  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
       e = list_next (e))
    {
      entry = list_entry (e, struct cache_entry, elem);
      if (entry->dirty && entry->sector != EMPTY)
       {
         block_write (fs_device, entry->sector, entry->data);      
	 entry->dirty = false;
       }
    }
   lock_release (&cache_lock);
}

/* Function exectued by the write-behind daemon */
void
write_behind_daemon (void *aux UNUSED)
{
  while (true)
   {
     buffer_cache_flush ();
     timer_sleep (FLUSH_FREQUENCY);
   }
}

void
cache_write (block_sector_t sector, void *buffer, int valid_bytes)
{
  struct cache_entry *entry = find_cache_entry (sector, false); 
  if (!entry)
   {
     entry = find_cache_entry (sector, true);
   }
 
  lock_acquire (&entry->update_lock);
  entry->open_count++;
  lock_release (&entry->update_lock);

  memcpy (entry->data, buffer, valid_bytes);
  entry->sector = sector;
  entry->valid_bytes = valid_bytes;
  entry->dirty = true;

  lock_acquire (&entry->update_lock);
  entry->open_count--;
  lock_release (&entry->update_lock);
}

struct cache_entry *
cache_read (block_sector_t sector, int read_bytes)
{
  int zero_bytes;
  struct cache_entry *entry = find_cache_entry (sector, false);
  if (!entry)
    {
      entry = find_cache_entry (sector, true);
      block_read (fs_device, sector, entry->data);
    }

  lock_acquire (&entry->update_lock);
  entry->open_count++;
  lock_release (&entry->update_lock);

  entry->valid_bytes = read_bytes;
  entry->sector = sector;
  zero_bytes = BLOCK_SECTOR_SIZE - entry->valid_bytes;
  if (zero_bytes != 0)
   {
     memset (entry->data + read_bytes, 0, zero_bytes);
   }
  lock_acquire (&entry->update_lock);
  entry->open_count--;
  lock_release (&entry->update_lock);

  return entry;
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
           unused    - Do we need to find an unused cache_entry?
   @retval: pointer to the cache_entry found */
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
     if (entry->sector == sector)
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

void
evict_cache_entry ()
{
  struct list_elem *e; 
  struct cache_entry *entry;
  bool found = false;

  while (true)
  {
     for (e = list_rbegin (&buffer_cache); e != list_rend (&buffer_cache);
          e = list_prev (e))
      { 
        entry = list_entry (e, struct cache_entry, elem);
        if (entry->open_count == 0)
         {
           found = true;
           break;
         }
      }
     if (found)
       break;
  }   

  if (entry->dirty)
   {
     block_write (fs_device, entry->sector, entry->data);
   }
  list_remove (e);
  free (entry);
}
