Design Document for Project 4: File System
==========================================

## Group Members

* Tornike Khachidze <tkhach14@freeuni.edu.ge>
* Irakli Chkuaseli <ichku14@freeuni.edu.ge>

## Buffer Cache

##### Data Structures

cache.c
```c
struct block_slot {
  block_sector_t sector;              /* Sector index in block device. */
  uint8_t data[BLOCK_SECTOR_SIZE];
  bool accessed;
  bool dirty;
  uint32_t user_count;

  struct lock lock;
};

struct block_slot *buffer_cache;
struct bitmap *free_slots;

struct lock cache_lock;
```

##### Solution



---

## Indexed and Extensible Files

##### Data Structures

inode.c
```c
struct inode_disk
  {
    ...
    block_sector_t direct[DIRECT_BLOCKS];
    block_sector_t indirect;
    block_sector_t doubly_indirect;
    ...
  };
```

##### Solution

---

## Subdirectories

##### Data Structures

thread.h
```c
struct inode *cwd_inode;                  /* Current working directory inode. */
```

inode.c
```c
struct inode_disk
  {
    ...
    bool is_dir;
    ...
  };
```

##### Solution

იმისათვის, რომ მომხმარებელს შეძლებოდა გამოეყენებინა როგორც სრული, ასევე რელატიური მისამართები ფაილებთან წვდომისთვის, დაგვჭირდა ყოველი პროცესისთვის შეგვენახა მისთვის აქტიური სამუშაო დირექტორია, რომელიც მის შვილებსაც გადაეცემათ. 

მიღებული მისამართის მიგნება ხდება `find_file()` ფუნქციით, რომელიც თავის მხრივ იყენებს `get_next_part()` parsing ფუნქციას. 

`find_file()` არკვევს რა სახის სტრინგია შემოსული და იმ შემთხვევაში, თუ მისამართი სრულია, ძებნას იწყებს root დირექტორიიდან, ხოლო თუ რელატიურია — კონკრეტული პროცესის სამუშაო დირექტორიიდან. 

`filesys_create()` ფუნქციაში გაერთიანებული გვაქვს როგორც ფაილის, ისე დირექტორიის შექმნა, ანუ ეს ერთი ფუნქცია გამოიყენება ორი syscall-ისთვის. დირექტორიის შექმნისას მასში მაშინვე იქმნება ორი ფაილი "." და "..", რომელიც შესაბამისად მიუთითებს ამ დირექტორიას და ამ დირექტორიის მშობელ დირექტორიას. ეს საშუალებას გვაძლევს მარტივად მივაგნოთ ფაილს რელატიური მისამართის შემოსვლის შემთხვევაში.
