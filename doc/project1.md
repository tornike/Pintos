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

/* Stores sleeping threads. */
static struct list wait_list;
```

##### Algorithms

>> A2: Briefly describe what happens in a call to timer_sleep(),
>> including the effects of the timer interrupt handler.

Busy waiting-ის ნაცვლად `timer_sleep` იძახებს `thread_sleep` ფუნქციას, 
რომელიც თავისთავად `running` ნაკადს ანიჭებს დაბლოკილ სტატუსს და ათავსებს `wait_list`-ში.
ნაკადის `tick_till_wait` ცვლადში ინახება tick რომელზეც ნაკადმა უნდა
გაიღვიძოს.

ნაკადის გაღვიძება ხდება `thread_tick` ფუნქციაში, რომელიც `timer_interrupt`-ში
გამოიძახება. ანუ ყოველ tick-ზე მოწმდება `wait_list`-ში მოთავსებული ნაკადებიდან
რომელიმეს გაღვიძების დრო ხო არ მოვიდა და თუ ასეა ნაკადს ენიჭება `ready` სტატუსი
და ემატება `ready_list`-ში.

>> A3: What steps are taken to minimize the amount of time spent in
>> the timer interrupt handler?

`wait_list`-ში ნაკადები სორტირებულია `tick_till_wait`-ის მიხედვით, ანუ ლისტის თავში
ყოველთვის ისეთი ელემენტებია რომლებმაც ყველაზე ადრე უნდა გაიღვიძონ, ეს ყველაფერი 
გათვალისწინებულია `wait_list`-ში ჩამატებისას,
ეს საშუალებას გვაძლევს ყოველ ჯერზე `wait_list`-ს ბოლომდე არ გადავუყვეთ,
პირველივე ისეთი ნაკადის შეხვედრისას, რომელიც ვერ იღვიძებს ციკლიდან გამოვდივართ,
რადგან მისი შემდეგი ნაკადების დროც არ იქნება გასული.

##### Synchronization

>> A4: How are race conditions avoided when multiple threads call
>> timer_sleep() simultaneously?

`thread_sleep`-ის გამოძახებისას, სადაც იცვლება გლობალური ლისტები, ინტერაფტები
გათიშულია, ანუ race conditions-ი აღარ ხდება. `thread_sleep`-ის დასრულებისას
ინტერაფტები ისევ ირთვება.

>> A5: How are race conditions avoided when a timer interrupt occurs
>> during a call to timer_sleep()?

რადგანაც ინტერაფტები გათიშულია ეს პრობლემას აღარ წარმოადგენს.

##### Rationale

>> A6: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

სხვა დიზაინზე არც გვიფიქრია, პირველივე რაც მოგვაფიქრდა ის დავწერეთ,
რადგან რთულ ამოცანას არ წარმოადგენდა და პირობა თავად კარნახობს
როგორ უნდა დაწერილიყო, ჩვენი აზრით სხვანაირად ვერც იმპლემენტირდებოდა.

--- 

## Priority Scheduling

##### Data Structures

```c
// thread struct
int saved_priority;
struct list locks;                  /* Locks this thread holds and got donation */
struct lock* block_lock;            /* Lock this thread is waiting on */

// lock struct
struct thread* holders_donor;
struct list_elem elem;

bool thread_cmp_priority (const struct list_elem *, const struct list_elem *, void *);
void thread_donate_priority(struct thread*, struct lock*);
void thread_undonate_priority(void);

static bool thread_cmp_wait_times (const struct list_elem *, const struct list_elem *);
static bool sem_cmp_priority (const struct list_elem *, const struct list_elem *, void *);
```

##### Algorithms

>> B3: How do you ensure that the highest priority thread waiting for
>> a lock, semaphore, or condition variable wakes up first?

`lock`-ის და `semaphore`-ის შემთხვევაში ყველაფერი მარტივადაა, `semaphore`-ის მომლოდინეთა
სიაში ნაკადები ყოველთვის კლებადობითაა დალაგებული, რადგან ჩამატება `list_insert_ordered`-ით ხდება,
ანუ თავში მაღალი პრიორიტეტეის ნაკადები ხვდება. `sema_up` კი ყოველთვის ლისტის პირველ ელემენტს იღებს, ანუ
ჯერ მაღალი პრიორიტეტის ნაკადები ეშვება. რადგანაც `lock` `semaphore`-ზეა დაშენებულია ყველაფერი
ავტომატურად მისთვისაც მუშაობს.

პატარა განსხვავებაა `condition variable`-ის შემთხვევაში, აქ მომლოდინეთა სიაში პირდაპირ ნაკადების 
ნაცვლად `semaphore_elem`-ებია, ამიტომ `semaphore_elem`-ში ჩავამატეთ `priority` ცვლადი, რომელიც
ამ `semaphore`-ის `holder`-ის პრიორიტეტია, დანარჩენი ყველაფერი იგივეა, ანუ `cond_signal` ყოველთვის
მაღალ პრიორიტეტულ `semaphore`-ს იღებს და მის `holder`-ს აღვიძებს.

>> B4: Describe the sequence of events when a call to lock_acquire()
>> causes a priority donation.  How is nested donation handled?

`lock_acquire`-ში მოწმდება, თუ ლოქს `holder`-ი ყავს და ფუნქციის გამომძახებლის
პრიორიტეტი `holder`-ისაზე დიდია ხდება დონაცია `running` ნაკადიდან `holder`-ზე.
ამ ყველაფერს `thread_donate_priority` ფუნქცია ასრულებს. `holder`-ის პრიორიტეტი
ინახება `saved_priority`-ში და უსეტდება `running`-ის პრიორიტეტი.
რადგანაც `ready_list`-ში მყოფ ნაკადს პრიორიტეტი ეცვლება მას თავიდან ვამატებთ
ლისტში, რათა სწორ ადგილას ჩაჯდეს.

ფუნქციის ბოლოში მოწმდება, თუ `holder` ნაკადი რომელიმე ლოქზეა, 
რომელიც `block_lock`-ში ინახება, დაბლოკილი ხდება `thread_donate_priority` ფუნქციის 
რეკურსიული გამოძახება `holder`-ისა და `holder`-ის `block_lock`-ისთვის, შედეგად პრიორიტეტი
იმ ნაკადს მისდის რომელსაც ყველა დანარჩენი ელოდება.

>> B5: Describe the sequence of events when lock_release() is called
>> on a lock that a higher-priority thread is waiting for.

`lock_release`-ში მოწმდება, თუ ლოქის `holder`-ზე დონაცია მოხდა გამოიძახება
`thread_undonate_priority` ფუნქცია, რომელიც თავისთავად, ან ნაკადს საწყის პრიორიტეტს
უბრუნებს, ან თუ სხვა ნაკადებიც ელოდებიან ამ ლოქს მათ შორის მაქსიმალურ პრიორიტეტს უსეტავს.

##### Synchronization

>> B6: Describe a potential race in thread_set_priority() and explain
>> how your implementation avoids it.  Can you use a lock to avoid
>> this race?

##### Rationale

>> B7: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

პირველივე რაც მოვიფიქრეთ ეს იყო, მარტივი გადაწყვეტა იყო და მარტივადაც დაიწერა,
სკელეტონ კოდიც საერთოდ არ შეცვლილა, ზემოთ აღნიშნული if-ები ჩაემატა უბრალოდ.
დროის თვალსაზრისითაც ცუდი ვარიანტი არაა, ლისტებში ჩამატებებს თუ არ ჩავთვლით O(1)-ში
მუშაობს. ამიტომაც სხვა გადაწყვეტაზე არც გვიფიქრია.

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
