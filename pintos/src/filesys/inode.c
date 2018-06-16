#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct blocks in inode_disk. */
#define DIRECT_BLOCKS 10

/* Number of block_sector_t entries in block */
#define RECORDS_IN_BLOCK (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //block_sector_t start;               /* First data sector. */
    block_sector_t end;                 /* Last data sector. !!!!!!*/
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[DIRECT_BLOCKS];
    block_sector_t indirect;
    block_sector_t doubly_indirect;
    uint32_t unused[113];               /* Not used. */
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
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  return pos / BLOCK_SECTOR_SIZE;
  // if (pos < inode->data.length)
  //   return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  // else
  //   return -1;
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

static block_sector_t get_disk_sector (struct inode_disk *disk_inode, block_sector_t file_sector) {
  if (file_sector < DIRECT_BLOCKS) {
    //printf("%u %u\n", file_sector, disk_inode->direct[file_sector]);
    return disk_inode->direct[file_sector];
  } else if (file_sector < DIRECT_BLOCKS + RECORDS_IN_BLOCK) {
    block_sector_t indirect[RECORDS_IN_BLOCK];
		cache_read (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, indirect);
    //printf("%u %u\n", file_sector, indirect[file_sector - DIRECT_BLOCKS]);
    return indirect[file_sector - DIRECT_BLOCKS];
  } else { /* Doubly Indirect */
    block_sector_t dindirect[RECORDS_IN_BLOCK];
		cache_read (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, dindirect);
    block_sector_t relative_index = file_sector - DIRECT_BLOCKS - RECORDS_IN_BLOCK;
    block_sector_t outer_index = relative_index / RECORDS_IN_BLOCK;
    cache_read (dindirect[outer_index], 0, 0, BLOCK_SECTOR_SIZE, dindirect);
    block_sector_t inner_index = relative_index % RECORDS_IN_BLOCK;
    //printf("%u %u %u %u %u\n", file_sector, relative_index, outer_index, inner_index, dindirect[inner_index]);
    return dindirect[inner_index];
  }
}

static int recursive_allocation (uint32_t depth, block_sector_t *table, struct inode_disk *disk_inode, uint32_t index, size_t *sectors) {
  static char zeros[BLOCK_SECTOR_SIZE];
  int written_sectors = 0;
	if (depth == 0) {
		while (index < RECORDS_IN_BLOCK) {
			if (!free_map_allocate (1, &table[index]))
        return -1; /* No Space */
      cache_write (table[index], 0, 0, BLOCK_SECTOR_SIZE, zeros);
      //printf("Write: %u %u %u\n", disk_inode->end, table[index], index);
			index++;
      written_sectors++;
			disk_inode->end++;
      if (--(*sectors) == 0) break;
		}
    return written_sectors;
	} else {
		uint32_t outer_index = index / 128; // pow(128, depth);
    uint32_t inner_index = index % 128; // pow(128, depth);
		while (outer_index < RECORDS_IN_BLOCK) {
      if (inner_index == 0 && !free_map_allocate(1, &table[outer_index]))
        return -1;
      block_sector_t *inner_table = malloc(BLOCK_SECTOR_SIZE);
			cache_read (table[outer_index], 0, 0, BLOCK_SECTOR_SIZE, inner_table);
      //printf("%u %u %u\n", *sectors, inner_index, inner_table[inner_index]);
			written_sectors = recursive_allocation(depth - 1, inner_table, disk_inode, inner_index, sectors);
      cache_write(table[outer_index], 0, 0, BLOCK_SECTOR_SIZE, inner_table);
      //printf("%u %u %u\n", *sectors, inner_index, inner_table[inner_index]);
      free(inner_table);
      if (*sectors == 0) return written_sectors;
      if (written_sectors == -1) return -1; /* No Space */
			outer_index++;
      inner_index = outer_index * 128; // pow(128, depth);
		}
	}
  NOT_REACHED ();
}

static bool inode_allocate_sectors (struct inode_disk *disk_inode, size_t sectors) {
  if (sectors == 0) return true;

  static char zeros[BLOCK_SECTOR_SIZE];

  bool indirect_init = disk_inode->indirect == block_size(fs_device);  /* Does Indirect table needs initialization */
  bool dindirect_init = disk_inode->doubly_indirect == block_size(fs_device);  /* Does Doubly Indirect table needs initialization */

  while (disk_inode->end < DIRECT_BLOCKS) {
    if (!free_map_allocate (1, &disk_inode->direct[disk_inode->end]))
      return false;
    cache_write (disk_inode->direct[disk_inode->end], 0, 0, BLOCK_SECTOR_SIZE, zeros);
    //printf("Write: %u %u\n", disk_inode->end, disk_inode->direct[disk_inode->end]);
    disk_inode->end++;
    if (--sectors == 0) return true;
  }
  
  block_sector_t table[RECORDS_IN_BLOCK];
  int res;

  if (indirect_init) free_map_allocate(1, &disk_inode->indirect); //!!!
  cache_read (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, table);
  res = recursive_allocation(0, table, disk_inode, disk_inode->end - DIRECT_BLOCKS, &sectors);
  cache_write (disk_inode->indirect, 0, 0, BLOCK_SECTOR_SIZE, table);
  if (sectors == 0) return true;
  if (res == -1) return false; /* Allocation failed */

  if (dindirect_init) free_map_allocate(1, &disk_inode->doubly_indirect); //!!!
  cache_read (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, table);
  res = recursive_allocation(1, table, disk_inode, disk_inode->end - DIRECT_BLOCKS - RECORDS_IN_BLOCK, &sectors);
  cache_write (disk_inode->doubly_indirect, 0, 0, BLOCK_SECTOR_SIZE, table);
  if (sectors == 0) return true;
  if (res == -1) return false; /* Allocation failed */
  
  return false;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
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
      disk_inode->indirect = block_size(fs_device);
      disk_inode->doubly_indirect = block_size(fs_device);
      //printf("%u %u\n", length, sectors);

      if (!inode_allocate_sectors(disk_inode, sectors)) {
        // free tables.
        free (disk_inode);
      } else {
        cache_write (sector, 0, 0, BLOCK_SECTOR_SIZE, disk_inode);
        free (disk_inode);
        success = true;
      }
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
  cache_read (inode->sector, 0, 0, BLOCK_SECTOR_SIZE, &inode->data);
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
          //free_map_release (inode->sector, 1);
          //free_map_release (inode->data.start,
                            //bytes_to_sectors (inode->data.length));
        }
      cache_write (inode->sector, 0, 0, BLOCK_SECTOR_SIZE, &inode->data);
      free (inode);
    }
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
  uint8_t *bounce = NULL;

  if (offset >= inode->data.length) {
    return bytes_read;
  }

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = get_disk_sector(&inode->data, byte_to_sector (inode, offset));
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
  free (bounce);

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
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  off_t new_size = offset + size;
  if (new_size > inode->data.length) {
    size_t needed_sectors = DIV_ROUND_UP(new_size, BLOCK_SECTOR_SIZE) - inode->data.end;
    //printf("MEVIIIDAA %u\n", needed_sectors);
    //printf("%u %u %u\n", new_size, inode->data.length, offset);
    inode_allocate_sectors(&inode->data, needed_sectors);
    inode->data.length = new_size;
    //printf("%u %u\n", inode->data.end, inode->data.length);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = get_disk_sector(&inode->data, byte_to_sector (inode, offset));
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
  free (bounce);

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
  return inode->data.length;
}
