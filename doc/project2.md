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
    char file_name[512];
    char cmd_line[512];

    bool load_success;
    struct semaphore load_signal;
    struct thread* parent;
};
```

##### Algorithms

არგუმენტებს, სემაფორას და პროცესის მშობელს ვინახავთ ჩვენს შექმნილ `process_argument_data` სტრუქტურაში, სტრუქტურისთვის გამოყოფილი გვაქვს ერთი ფეიჯი, სადაც ვინახავთ ამ მონაცემებს, რათა არ მოხდეს race condition მშობელსა და შვილებს შორის. როცა ეს სტრუქტურა აღარ გვჭირდება, ცხადია მას ვასუფთავებთ მეხსიერებიდან. შემდეგ `PHYS_BASE`-დან ქვემოთ იზრდება სტეკი პროცესისთვის გადმოცემული არგუმენტებით, რომელიც ტოკენაიზერით იყოფა.

##### Synchronization

# აქ დაამატე რა სინქრონიზაციაც გვაქვს პროცესში. აღარ მახსომს :)

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

##### Rationale

---

## Advanced Scheduler

##### Data Structures

```c
int nice;
fixed_point_t recent_cpu;

static fixed_point_t load_avg;

static bool lock_donor_cmp (const struct list_elem *, const struct list_elem *, void *);
static void update_priority(struct thread *, void *);
static void update_recent_cpu(struct thread *, void *);
static void update_load_avg();

bool timer_should_update(void);
```

##### Algorithms

>> C2: Suppose threads A, B, and C have nice values 0, 1, and 2.  Each
>> has a recent_cpu value of 0.  Fill in the table below showing the
>> scheduling decision and the priority and recent_cpu values for each
>> thread after each given number of timer ticks:

timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | thread to run
------------|------|------|------|------|------|------|--------------
 0          |  0   |  0   |  0   |  63  |  61  |  59  |       A
 4          |  4   |  0   |  0   |  62  |  61  |  59  |       A
 8          |  8   |  0   |  0   |  61  |  61  |  59  |       A
12          |  12  |  0   |  0   |  60  |  61  |  59  |       B
16          |  12  |  4   |  0   |  60  |  60  |  59  |       B
20          |  12  |  8   |  0   |  60  |  59  |  59  |       A
24          |  16  |  8   |  0   |  59  |  59  |  59  |       A
28          |  20  |  8   |  0   |  58  |  59  |  59  |       C
32          |  20  |  8   |  4   |  58  |  59  |  58  |       B
36          |  20  |  12  |  4   |  58  |  58  |  58  |       B

>> C3: Did any ambiguities in the scheduler specification make values
>> in the table uncertain?  If so, what rule did you use to resolve
>> them?  Does this match the behavior of your scheduler?

პირობიდან არ ირკვეოდა ჯერ `recent_cpu` და `load_avg` უნდა დაგვეთვალა და მერე გაგვეახლებინა ნაკადების პრიორიტეტი, თუ პირიქით. ჩვენ ჯერ `recent_cpu` და `load_avg` ვითვლით და შემდეგ ვცვლით ნაკადების პრიორიტეტებს, რაც ტესტებიდან ჩანს, რომ სწორი მიდგომაა. ასევე პირობაში არაა დაკონკრეტებული რა უნდა მოხდეს იმ შემთხვევაში, თუ მიმდინარე ნაკადის პრიორირტეტი ტოლი გახდება სხვა ნაკადებისა, ანუ უნდა მოხდეს თუ არა `yield` ამ შემთხვევაში, ამას არ ვაკეთებთ და ველოდებით როდის იქნება მიმდინარე ნაკადის პრიორიტეტი მკაცრად ნაკლები, რომ გადავერთოთ სხვაზე.

>> C4: How is the way you divided the cost of scheduling between code
>> inside and outside interrupt context likely to affect performance?

რადგან `recent_cpu` და `load_avg` ყოველ წამში, ხოლო `priority` ყოველ მეოთხე tick-ზე იცვლება, ცხადია ეს გამოთვლები გავლენას იქონიებს პროგრამის წარმადობაზე. ძალიან ბევრი ნაკადების არსებობის შემთხვევაში შესაძლოა მოხდეს შესამჩნევი დაყოვნება.

##### Rationale

>> C5: Briefly critique your design, pointing out advantages and
>> disadvantages in your design choices.  If you were to have extra
>> time to work on this part of the project, how might you choose to
>> refine or improve your design?

მეტი გაუმჯობესების საშუალებას საწყისი კოდი არ გვაძლევს.
