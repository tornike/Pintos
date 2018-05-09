Design Document for Project 2: User Program
======================================

## Group Members

* Tornike Khachidze <tkhach14@freeuni.edu.ge>
* Irakli Chkuaseli <ichku14@freeuni.edu.ge>

---

## Process

##### Data Structures

process.h
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

##### Solution

`process_argument_data` სტრუქტურის შექმნა დაგვჭირდა, რათა `process_execute()`-ში
`start_process()`-ისთვის არგუმენტებთან ერთად სხვა საჭირო ცვლადებიც გადაგვეცა.
ამიტომ სტრინგის ნაცვლად ახალი page სტრუქტურას გამოვუყავით და მისი მისამართი გადავეცით
`start_process()`-ს. `process_execute()`-ის ბოლოს სტრუქტურის დეალოკაცია ხდება.

იმ შედეგის მისაღწევად, რომ `process_execute()`-მა არ დააბრუნოს მნიშვნელობა სანამ
`load`-ის შედეგი ცნობილი არ იქნება, ვიყენებთ სემაფორით სინქრონიზაციას. `thread_create()`-ის
გამოძახების შემდეგ, თუ კერნელის ნაკადი წარმატებით შეიქმნა მშობელი ნაკადი სემაფორაზე ჩერდება
და ელოდება `load`-ის შედეგს. ამავდროულად შვილი `start_process()`-ის შესრულებას იწყებს და
იძახებს `load()`-ს, სადაც ხდება `setup_stack()`-ის გამოძახება, რომელშიც გადმოცემული `cmd_line`
სტრინგი იხლიჩება ტოკენაიზერით და შემდეგ სათითაო არგუმენტი იწერება სტეკში მიმდევრობით, ისე,
როგორც პირობაშია მოცემული. როდესაც `load`-ის შედეგი ცნობილი ხდება, მნიშვნელობა `load_signal`-ში
ინახება, ასევე წარმატებისშემთხვევაში ნაკადი თავის თავს შვილად ამატებს `parent`-ის შვილებში,
და ხდება `load_signal`სემაფორის მნიშვნეობის გაზრდა, რითაც შვილი ნაკადი მშობელს აცნობებს,
რომ `load`-ის შედეგი მზადაა.

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

thread.h
```c
    struct file* file_descriptors[MAX_FILE_COUNT];
    int next_free_fd;

    struct file* exec_file;

    struct list_elem child_elem;
    struct list children;
    int exit_status;
    struct semaphore wait_for_parent;   /* Used to wait for parent to take status */
    struct semaphore status_ready;      /* Signal parent to take status */
```

##### Solution

თითოეული syscall-ის გამოძახებისას პირველ რიგში მოწმდება რომ სტეკის და ყველა არგუმენტად
გადმოცემული ფოინთერი user space-შია `is_valid_ptr()` ფუნქციით, ასევე მოწმდება სტრინგების
ვალიდურობა `is_valid_string()` ფუნქციით. თუ მაინც მოხდა page fault ანუ არარსებულ page-ზე
მომხდარა მიმართვა და ეს ქეისი მარტივად მოწმდება `page_fault()`-ში, ასეთ შემთხვევაში user
process კვდება და არა კერნელი.

ფაილური სისტემის syscall-ების შემთხვევაში უბრალოდ შესაბამისი ფაილური სისტემის
ფუნქციის გამოძახება ხდება, ეს ფუნქციები ყველგან დაცულია `filesys_lock`-ით, ანუ ფაილური
სისტემის კოდი პარალელურად არ სრულდება.

`exec` და `wait` syscall-ებისთვის აუცილებელი იყო შვილების დამახსოვრება, ამისთვის `thread`
სტრუქტურაში ჩავამატეთ ახალი `list`-ი და `list_elem`-ი. კონკრეტული პროცესის
შვილები ერთმანეთს უკავშირდება `child_elem`-ით და ინახება `children` ლისტში.
ლისტში კონკრეტული შვილის ძებნა ხდება `tid_t` ცვლადის დახმარებით ლისტზე გადაყოლით.

exec-ში უბრალოდ ხდება `process_execute()`-ის გამოძახება, რომლის მუშაობის პრინციპი
უკვე აღვწერეთ.

wait `syscall`-ისთვის thread-ში დაგვჭირდა სემაფორების და `exit_status`-ის
დამატება. `exit_status` თავიდან არის -1,  იმ შემთხვევისთვის თუ პროცესი კერნელმა მოკლა,
ამიტომ `exit` სისტემ ქოლის გამოძახება ვერ მოასწრო, მშობელს -1 სტატუსი დაუბრუნდეს.

იმისათვის, რომ მშობელს ნებისმიერ დროს შეეძლოს მისი შვილის სტატუსის მიღება ვიყენებთ ორი
სემაფორით სინქრონიზაციას.
`status_ready` არის მშობლისთვის რათა შვილს დაელოდოს სანამ შვილის სტატუს კოდი მზად
არ იქნება. როდესაც შვილი დაასრულებს მუშაობას სემაფორის მნიშვნელობის გაზრდით მშობელს
აცნობებს, რომ მისი სტატუსი მზადაა და მშობელი მას წაიკითხავს.
ხოლო `wait_for_parent` არის შვილისთვის, რათა მშობელს დაელოდოს მანამ სანამ მისი სტატუსის
წაკითხვა არ მოხდება, ანუ thread სტრუქტურა არსებობს და მეხსიერებიდან არ იშლება სანამ
მშობელი მის სტატუსს არ მიიღებს. როდესაც მშობელი ადრე თუ გვიან `wait`-ს გამოიძახებს
`wait_for_parent` სემაფორის გაზრდით შვილს აცნობებს, რომ მისი სტატუსი საჭირო აღარაა, შედეგად
`thread_exit()` ბოლომდე შესრულდება და ნაკადი მეხსიერებიდან წაიშლება.

ასევე გათვალისწინებული გვაქვს ის შემთხვევა როდესაც მშობელი ისე კვდება, რომ რომელიმე შვილზე
`wait`-ს არ იძახებს. ნებისმიერი პროცესის სიკვდილისას `process_exit()`-ში პროცესი ყველა
არსებულ შვილს გადაყვება და წინასწარ გაზრდის wait_for_parent სემაფორის მნიშვნელობებს, რათა
შვილები `thread_exit()`-ში აღარ გაჩერდნენ და პირდაპირ მოხდეს მათი დეალოკირება.