Design Document for Project 2: User Program
======================================

## Group Members

* Tornike Khachidze <tkhach14@freeuni.edu.ge>
* Irakli Chkuaseli <ichku14@freeuni.edu.ge>
* Mikheil Zhghenti <mzhgh14@freeuni.edu.ge>
* Ramazi Goiati <rgoia14@freeuni.edu.ge>

---

## Process

##### Data Structures

```c
struct process_argument_data 
{
    char file_name[BUFFER_SIZE];
    char cmd_line[BUFFER_SIZE];

    bool load_success;
    struct semaphore load_signal;
    struct thread* parent;
};
```

##### Algorithms

არგუმენტებს, სემაფორას და პროცესის მშობელს ვინახავთ ჩვენს შექმნილ `process_argument_data` სტრუქტურაში, სტრუქტურისთვის გამოყოფილი გვაქვს ერთი ფეიჯი, სადაც ვინახავთ ამ მონაცემებს, რათა არ მოხდეს race condition მშობელსა და შვილებს შორის. როცა ეს სტრუქტურა აღარ გვჭირდება, ცხადია მას ვასუფთავებთ მეხსიერებიდან. შემდეგ `PHYS_BASE`-დან ქვემოთ იზრდება სტეკი პროცესისთვის გადმოცემული არგუმენტებით, რომელიც ტოკენაიზერით იყოფა.

##### Synchronization

# აქ დაამატე რა სინქრონიზაციაც გვაქვს პროცესში. აღარ მახსომს

--- 

## Syscalls

##### Data Structures

```c
struct lock filesys_lock;

static void write (struct intr_frame *f);
static void read (struct intr_frame *f);
static void filesize (struct intr_frame *f);
static void seek (struct intr_frame *f);
static void tell (struct intr_frame *f);
static void exec (struct intr_frame *f);
static void wait (struct intr_frame *f);
static void open (struct intr_frame *f);
static void close (struct intr_frame *f);
static void create (struct intr_frame *f);
static void remove (struct intr_frame *f);
static void exit (struct intr_frame *f);

static bool is_valid_ptr (const void *, size_t);
static bool is_valid_string (const void *);
```

##### Algorithms

თითოეული `syscall`-ის გამოძახებდამდე ვამოწებთ მემორისა და გადმოცემული სტრინგის ვალიდურობას ჩვენი დაწერილი `is_valid_ptr()` და `is_valid_string()` ფუნქციებით, რომელიც უზრუნველყოფს, რომ სტრინგი სრულდება `\0`-ით ხოლო მისამართი კი მიუთითებს user space-ში და არა კერნელისთვის გამოყოფილ ადგილას.

##### Synchronization

ყოველი `syscall`, რომელსაც შეხება აქვს ფაილურ სისტემასთან იყენებს სინქრონიზაციას. ამისთვის ვიყენებთ ერთ `lock`-ს, რომელიც მთლიანად ზღუდავს წვდომას მასთან, სანამ რომელიმე ნაკადი ფაილებთან მუშაობს.