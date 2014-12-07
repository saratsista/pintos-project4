#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

/* Enum for different types of pointers in inode_disk */
enum pointer
 {
   NONE,
   DIRECT,		/* Direct pointer to sector on-disk */
   INDIRECT,		/* Indirect pointer to sectors */
   DOUBLE_INDIRECT	/* Double Indirect pointer to sectors */
 };

/* An Indirect Pointer */
struct indirect
 {
   block_sector_t sector;  /* Sector where indirect pointer is present */
   off_t offset;	   /* offset into SECTOR */
 };

/* A Double-indirect Pointer */
struct d_indirect
 {
   block_sector_t sector;
   off_t off1;		  /* Offset into SECTOR */
   off_t off2;		  /* Offset into sector found at off1 */
 };

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

void inode_deallocate (struct inode *);
int inode_allocate_indirect (struct indirect *,int);
int inode_allocate_double_indirect (struct d_indirect *,int);

bool grow_file (struct inode *, off_t); 

#endif /* filesys/inode.h */
