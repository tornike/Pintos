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
};

struct block_slot *buffer_cache;
struct bitmap *free_slots;

struct lock cache_lock;

void cache_read (block_sector_t sector, int sector_ofs, off_t buffer_ofs, size_t size, void *buffer_);
void cache_write (block_sector_t sector, int sector_ofs, off_t buffer_ofs, size_t size, const void *buffer_);
```

##### Solution

ჩვენი ქეში წარმოადგენს `block_slot` სტრუქტურების მასივს. ყველანაირი მოქმედება რეალურ დისკთან ხდება ქეშის გავლით, ანუ
ფაილური სისტემა მხოლოდ ქეშთან მუშაობს, ხოლო ქეშიდან დისკზე ბლოკების გადაწერე მხოლოდ საჭიროების შემთხვევაში ხდება, ამის 
მაგალითებია ქეშიდან ბლოკის გაძევება და შესაბამისი `inode`-ს დახურვა.

სინქრონიზაციისთვის ვიყენებთ ერთ გლობალურ ლოქს, რომელიც გამოიყენება მხოლოდ ქეშში ძებნისას, ან ბლოკის ქეშში ჩაწერისას,
ხოლო სხვადასხვა სლოტებიდან, ანუ სექტორებიდან, წერა კითხვა მიმდინარეობს პარალელურად.

---

## Indexed and Extensible Files

##### Data Structures

inode.c
```c
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

static block_sector_t get_disk_sector (const struct inode_disk *disk_inode, block_sector_t file_sector);
static bool inode_grow (struct inode_disk *disk_inode, size_t sectors);
```

##### Solution

`Extensible Files`-ის იმპლემენტაციისას გამოვიყენეთ სტანდარტული `Indexed` სტრუქტურა. `inode`-ს კვლავ გონია, რომ მთელი მისი 
კონტენტი დისკზე მიყოლებით სექტორებშია განთავსებული, სინამდვილეში კი ყოველი მიმართვისას `get_disk_sector` დამხმარე ფუნქციით 
`file_sector`-ი, ანუ ლოგიკური სექტორი, ითარგმნება რეალურ დისკის სექტორში.

სტრუქტურიდან გამომდინარე `inode`-ს გაზრდაც მარტივია. საქმის გასამარტივებლად `start`-ის ნაცვლად inode_disk-ში `end` შევინახეთ,
რაც კონტენტის ბოლო სექტორის შემდეგ სექტორს, ანუ `inode`-ს პირველ არაათვისებულ ლოგიკურ სექტორს წარმოადგენს. გაზრდისას უბრალოდ
`end`-იდან მივუყვებით და თავისუფალი სექტორების ინდექსებს `Indexed` სტრუქტურაში შესაბამის ადგილებზე ვწერთ.

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

files.h
```c
struct opened_file {
  int descriptor;
  void *file;
  bool is_dir;
  struct hash_elem elem;
};
```

##### Solution

იმისათვის, რომ მომხმარებელს შეძლებოდა გამოეყენებინა როგორც სრული, ასევე რელატიური მისამართები ფაილებთან წვდომისთვის, დაგვჭირდა ყოველი პროცესისთვის შეგვენახა მისთვის აქტიური სამუშაო დირექტორია, რომელიც მის შვილებსაც გადაეცემათ. აქტიური დირექტორიას ვინახავთ
inode-ს სახით, ანუ სანამ რომელიმე პროცესი შესაბამის დირექტორიას იყენებს დისკიდან არ წაიშლება.

მიღებული მისამართის მიგნება ხდება `find_file()` ფუნქციით, რომელიც თავის მხრივ იყენებს `get_next_part()` parsing ფუნქციას. 

`find_file()` არკვევს რა სახის სტრინგია შემოსული და იმ შემთხვევაში, თუ მისამართი სრულია, ძებნას იწყებს root დირექტორიიდან, ხოლო თუ რელატიურია — კონკრეტული პროცესის სამუშაო დირექტორიიდან. 

`filesys_create()` ფუნქციაში გაერთიანებული გვაქვს როგორც ფაილის, ისე დირექტორიის შექმნა, ანუ ეს ერთი ფუნქცია გამოიყენება ორი syscall-ისთვის. დირექტორიის შექმნისას მასში მაშინვე იქმნება ორი ფაილი "." და "..", რომელიც შესაბამისად მიუთითებს ამ დირექტორიას და ამ დირექტორიის მშობელ დირექტორიას. ეს საშუალებას გვაძლევს მარტივად მივაგნოთ ფაილს რელატიური მისამართის შემოსვლის შემთხვევაში.

`filesys_open()`-ის შემთხვევაშიც is_dir ცვლადის შესაბამისად ხდება ფაილის ან დირექტორიის გახსნა და მისი მისამართის დაბრუნება.
ყოველი პროცესი მეტა ინფორმაციად ინახავს შესაბამის `file_descriptor`-ზე ფაილია გახსნილი თუ დირექტორია, შედეგად მარტივდება შესაბამისი
`syscall`-ების შესრულება.

---

## Synchronization

##### Solution

რაც შეეხება სინქრონიზაციას, გლობალური ლოქი მოვაშორეთ და ჩავუმატეთ ყველა `inode`-ს `directory`-ს საკუთარი, შედეგად
სხვადასხვა `inode`-ზე და სხვადასხვა `directory`-ებზე მიმართვები ხდება პარალელურად. ასევე დაგვჭირდა რამოდენიმე გლობალური ლოქის
დამატება გლობალური სტრუქტურების დასაცავად. 
