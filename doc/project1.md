Design Document for Project 1: Threads
======================================

## Group Members

* Irakli Chkuaseli <ichku14@freeuni.edu.ge>
* Tornike Khachidze <tkhach14@freeuni.edu.ge>
* Mikheil Zhghenti <mzhgh14@freeuni.edu.ge>
* Ramazi Goiati <rgoia14@freeuni.edu.ge>

---

## Alarm Clock

##### Data Structures

```c
int64_t tick_till_wait;
```

##### Algorithms

>> A2: Briefly describe what happens in a call to timer_sleep(),
>> including the effects of the timer interrupt handler.

>> A3: What steps are taken to minimize the amount of time spent in
>> the timer interrupt handler?

##### Synchronization

>> A4: How are race conditions avoided when multiple threads call
>> timer_sleep() simultaneously?

>> A5: How are race conditions avoided when a timer interrupt occurs
>> during a call to timer_sleep()?

##### Rationale

>> A6: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

--- 

## Priority Scheduling

##### Data Structures

```c
int saved_priority;
struct list locks;
struct lock* block_lock;

bool thread_cmp_priority (const struct list_elem *, const struct list_elem *, void *);
void thread_donate_priority(struct thread*, struct lock*);
void thread_undonate_priority(void);

static bool thread_cmp_wait_times (const struct list_elem *, const struct list_elem *);
static bool sem_cmp_priority (const struct list_elem *, const struct list_elem *, void *);
```

##### Algorithms

>> B3: How do you ensure that the highest priority thread waiting for
>> a lock, semaphore, or condition variable wakes up first?

>> B4: Describe the sequence of events when a call to lock_acquire()
>> causes a priority donation.  How is nested donation handled?

>> B5: Describe the sequence of events when lock_release() is called
>> on a lock that a higher-priority thread is waiting for.

##### Synchronization

>> B6: Describe a potential race in thread_set_priority() and explain
>> how your implementation avoids it.  Can you use a lock to avoid
>> this race?

##### Rationale

>> B7: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

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

მეტი გაუმჯობესების საშუალებას base კოდი არ გვაძლევს.