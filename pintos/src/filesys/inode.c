#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct blocks in inode_disk. */
#define DIRECT_BLOCKS 122

/* Number of block_sector_t entries in block */
#define RECORDS_IN_BLOCK (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t end;                 /* Last + 1 data sector. */
    off_t length;                       /* File size in bytes. */
    block_sector_t direct[DIRECT_BLOCKS];
    block_sector_t indirect;
    block_sector_t doubly_indirect;
    bool is_dir;
    unsigned magic;                     /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

struct lock inodes_lock;

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock lock;
    struct inode_disk data;             /* Inode content. */
  };


static block_sector_t get_disk_sector (const struct inode_disk *disk_inode, block_sector_t file_sector) {
  block_sector_t result = block_size (fs_device);
  if (file_sector < DIRECT_BLOCKS) {
    return disk_inode->direct[file_sector];
  } else if (file_sector < DIRECT_BLOCKS + RECORDS_IN_BLOCK) {
    block_sector_t indirect[RECORDS_IN_BLOCK];
    cache_read (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, indirect);
    return indirect[file_sector - DIRECT_BLOCKS];
  } else if (file_sector < DIRECT_BLOCKS + RECORDS_IN_BLOCK * RECORDS_IN_BLOCK) { /* Doubly Indirect */ 
    block_sector_t dindirect[RECORDS_IN_BLOCK];
    cache_read (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, dindirect);
    block_sector_t index = file_sector - DIRECT_BLOCKS - RECORDS_IN_BLOCK;
    block_sector_t outer_index = index / RECORDS_IN_BLOCK;
    cache_read (dindirect[outer_index], 0, 0, BLOCK_SECTOR_SIZE, dindirect);
    block_sector_t inner_index = index % RECORDS_IN_BLOCK;
    return dindirect[inner_index];
  }
  ASSERT(result != block_size (fs_device));
  return result;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  block_sector_t file_sector = pos / BLOCK_SECTOR_SIZE;
  block_sector_t sector = get_disk_sector (&inode->data, file_sector);
  return sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&inodes_lock);
}

static void inode_destroy (struct inode_disk *disk_inode) {
  block_sector_t index = 0;
  block_sector_t *records = NULL;

  while (index < DIRECT_BLOCKS && index < disk_inode->end) {
    free_map_release (disk_inode->direct[index], 1);
    index++;
  }
  if (index == disk_inode->end) return;

  index -= DIRECT_BLOCKS;
  records = malloc (BLOCK_SECTOR_SIZE);
  cache_read (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
  while (index < RECORDS_IN_BLOCK && index + DIRECT_BLOCKS < disk_inode->end) {
    free_map_release (records[index], 1);
    index++;
  }
  free (records);
  free_map_release (disk_inode->indirect, 1);
  if (index + DIRECT_BLOCKS == disk_inode->end) return;

  index -= RECORDS_IN_BLOCK;
  records = malloc (BLOCK_SECTOR_SIZE);
  cache_read (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
  block_sector_t outer_index = index / RECORDS_IN_BLOCK;
  block_sector_t inner_index = index % RECORDS_IN_BLOCK;
  while (outer_index < RECORDS_IN_BLOCK) {
    block_sector_t inner_records[RECORDS_IN_BLOCK];
    cache_read (records[outer_index], 0, 0, BLOCK_SECTOR_SIZE, inner_records);
    while (inner_index < RECORDS_IN_BLOCK
          && inner_index + (outer_index + 1) * RECORDS_IN_BLOCK + DIRECT_BLOCKS < disk_inode->end) {
            free_map_release (inner_records[inner_index], 1);
            inner_index++;
          }
    free_map_release (records[outer_index], 1);
    if (inner_index + (outer_index + 1) * RECORDS_IN_BLOCK + DIRECT_BLOCKS == disk_inode->end) {
      free_map_release (disk_inode->doubly_indirect, 1);
      free (records);
      return;
    }
    outer_index++;
    inner_index %= RECORDS_IN_BLOCK;
  }
  free_map_release (disk_inode->doubly_indirect, 1);
  free (records);
}

static bool inode_grow (struct inode_disk *disk_inode, size_t sectors) {
  static char zeros[BLOCK_SECTOR_SIZE];

  block_sector_t index = disk_inode->end;
  block_sector_t *records = NULL;

  while (index < DIRECT_BLOCKS && sectors > 0) {
    if (!free_map_allocate (1, &disk_inode->direct[index]))
      return false;
    cache_write (disk_inode->direct[index], 0, 0, BLOCK_SECTOR_SIZE, zeros);
    disk_inode->end++;
    index++;
    sectors--;
  }
  if (sectors == 0) return true;

  if (index == DIRECT_BLOCKS) {
    if (!free_map_allocate(1, &disk_inode->indirect)) return false;
  }
  index -= DIRECT_BLOCKS;

  records = malloc (BLOCK_SECTOR_SIZE);
  cache_read (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
  while (index < RECORDS_IN_BLOCK && sectors > 0) {
    if (!free_map_allocate (1, &records[index])) {
      if (index == 0) free_map_release (disk_inode->indirect, 1);
      else cache_write (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
      return false;
    }
    cache_write (records[index], 0, 0, BLOCK_SECTOR_SIZE, zeros);
    disk_inode->end++;
    index++;
    sectors--;
  }
  cache_write (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
  free (records);
  if (sectors == 0) return true;


  if (index == RECORDS_IN_BLOCK) { 
    if (!free_map_allocate(1, &disk_inode->doubly_indirect)) return false;
  }
  index -= RECORDS_IN_BLOCK;

  records = malloc (BLOCK_SECTOR_SIZE);
  cache_read (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
  block_sector_t outer_index = index / RECORDS_IN_BLOCK;
  block_sector_t inner_index = index % RECORDS_IN_BLOCK;
  while (outer_index < RECORDS_IN_BLOCK && sectors > 0) {
    if (inner_index == 0 && !free_map_allocate (1, &records[outer_index])) {
      if (outer_index == 0) free_map_release (disk_inode->doubly_indirect, 1);
      else cache_write (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
      return false;
    }
    block_sector_t inner_records[RECORDS_IN_BLOCK];
    cache_read (records[outer_index], 0, 0, BLOCK_SECTOR_SIZE, inner_records);
    while (inner_index < RECORDS_IN_BLOCK && sectors > 0) {
      if (!free_map_allocate (1, &inner_records[inner_index])) {
        if (inner_index == 0) {
          free_map_release (records[outer_index], 1);
          if (outer_index == 0) free_map_release (disk_inode->doubly_indirect, 1);
          else cache_write (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
        }
        else cache_write (records[outer_index], 0, 0, BLOCK_SECTOR_SIZE, inner_records);

        return false;
      }
      cache_write (inner_records[inner_index], 0, 0, BLOCK_SECTOR_SIZE, zeros);
      disk_inode->end++;
      inner_index++;
      sectors--;
    }
    cache_write (records[outer_index], 0, 0, BLOCK_SECTOR_SIZE, inner_records);
    outer_index++;
    inner_index %= RECORDS_IN_BLOCK;
  }
  cache_write (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, records);
  free (records);
  if (sectors == 0) return true;
  return false;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
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
      size_t sectors = bytes_to_sectors (length);
      disk_inode->end = 0;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;

      if (!inode_grow(disk_inode, sectors)) {
        inode_destroy (disk_inode);
      } else {
        cache_write (sector, 0, 0, BLOCK_SECTOR_SIZE, disk_inode);
        success = true;
      }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire (&inodes_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release (&inodes_lock);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
    lock_release (&inodes_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);

  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);
  cache_read (inode->sector, 0, 0, BLOCK_SECTOR_SIZE, &inode->data);
  lock_release (&inodes_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    lock_acquire(&inode->lock);
    inode->open_cnt++;
    lock_release(&inode->lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
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

  lock_acquire (&inodes_lock);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      lock_release (&inodes_lock);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          inode_destroy (&inode->data);
        }
      free (inode);
    } else
    lock_release (&inodes_lock);
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

  lock_acquire (&inode->lock);

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read (sector_idx, sector_ofs, bytes_read, chunk_size, buffer);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  lock_release (&inode->lock);
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

  lock_acquire (&inode->lock);

  if (inode->deny_write_cnt) {
    lock_release (&inode->lock);
    return 0;
  }

  off_t new_size = offset + size;
  if (new_size > inode->data.length) {
    size_t needed_sectors = DIV_ROUND_UP(new_size, BLOCK_SECTOR_SIZE) - inode->data.end;
    if (inode_grow(&inode->data, needed_sectors)) {
      inode->data.length = new_size;
      cache_write (inode->sector, 0, 0, BLOCK_SECTOR_SIZE, &inode->data);
    }
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_write (sector_idx, sector_ofs, bytes_written, chunk_size, buffer);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  lock_release (&inode->lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  lock_acquire (&inode->lock);
  inode->deny_write_cnt++;
  lock_release (&inode->lock);
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
  lock_acquire (&inode->lock);
  inode->deny_write_cnt--;
  lock_release (&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{ 
  off_t length = inode->data.length;
  return length;
}

/* Returns if the inode is directory or not. */
bool
inode_is_dir (const struct inode *inode) 
{
  if (inode == NULL)
    return false;
  return inode->data.is_dir;
}