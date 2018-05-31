Design Document for Project 3: Virtual Memory
======================================

## Group Members

* Tornike Khachidze <tkhac14@freeuni.edu.ge>
* Irakli Chkuaseli <ichku14@freeuni.edu.ge>

---

## Demand Paging

##### Data Structures

page.h
```c
struct file_info {
    struct file *file;
    off_t offset;
    off_t length;
    bool mapped;
};

struct page {
    uint8_t *v_addr;        /* User space page address. */
    struct frame *frame;    /* Pointer to the frame which this page holds. */
    uint32_t *pagedir;      /* Page directory. */
    bool writable;
    struct file_info *file_info;
    swap_slot_t swap_slot;

    struct hash_elem elem;
};
```

frame.h
```c
struct frame {
  uint8_t *p_addr;          /* Physical address of the page. */
  struct page *u_page;      /* Pointer to user suplemental page. */
  bool pinned;

  struct list_elem elem;
};
```

##### Solution

`page` სტრუქტურა წარმოადგენს supplemental page-ს, რომელიც აღწერს რა უნდა ეწეროს მომხმარებლის
`v_addr` მისამართის ფეიჯზე. supplemental page table წარმოადგენს hash table-ს, რომლის ელემენტებიც
დაჰეშილია `v_addr`-ით. ეს hash table ინახება ყველა პროცესის thread სტრუქტურაში. hash table
ავირჩიეთ რათა supplemental page table-ში ძებნა შედარებით სწრაფი იყოს.

`frame` სტრუქტურა აღწერს ფიზიკურ ფრეიმს, მასში ინახება kernel address ანუ გამოყოფილი პეიჯის ფიზიკური 
მისამართი და page სტრუქტურის მისამართი, რომელსაც ეს ფრეიმი ეკუთვნის. frame table წარმოადგენს გლობალურ ლისტს.

საწყისი კოდისგან განსხვავებით, მეხსიერების გამოყოფისას რეალური ფეიჯის გამოყოფის მაგივრად ჩანაწერს
ვაკეთებთ supplemental page table-ში, მაშასადამე იმ მისამართებზე, რომელზეც მომხმარებელი ინფორმაციას
ელოდება supplemental page table-ში უნდა არსებობდეს ჩანაწერი. ასეთ შემთხვევაში page fault-ისას 
handler-ში იძახება `page_load()` ფუნქცია, რომელიც `frame allocation()`-ით იღებს თავისუფალ `frame`-ს 
და `page` სტრუქტურაში აღწერილი ცვლადების მიხედვით ხდება ინფორმაციის ჩატვირთვა: ჯერ მოწმდება პეიჯი
swap-ში ხო არაა გატანილი, შემდეგ ფაილიდან ხო არ უნდა მოხდეს ჩატვირთვა, სხვა შემთხვევაში კი გამოყოფილი
ფეიჯი უბრალოდ ნულდება.

page fault-ში ცალკე განიხილება სტეკის შემთხვევა, სადაც მოწმდება რა როგორც PUSH და PUSHA, ასევე
SUB ინსტრუქციები. SUB-ის შემთხვევაში supplemental page table-ში ახალ stack pointer-მდე ყველა 
ფეიჯისთვის კეთდება ჩანაწერი. სხვა ისეთ შემთხვევაში, როდესაც supplemental page table-ში ჩანაწერი
არ არსებობს პროცესი კვდება.

---

## Memory Map

##### Data Structures

mmap.h
```c
struct mmap {
    mapid_t mapping;
    struct file *file;
    uint8_t *start_addr;
    uint8_t *end_addr;

    struct hash_elem elem;
};
```

##### Solution

memory mapping-ის აღსაწერად დაგვჭირდა `mmap` სტრუქტურის შექმნა, რომელიც ინახავს იდენთიფიკატორს,
რომელი ფაილია დამეპილი და მომხმარებლის საწყის და საბოლოო ფეიჯებს, სადაც ეს ფაილია დამეპილი.
დანარჩენი ლოგიკა გატანილია supplemental page-ში, მომხმარებლის ნებისმიერ დამეპილ ფეიჯზე 
მიმართვისას, რადგანაც წინასწარ არსებობს supplemental page, შესაბამისი ინფორმაციის ჩატვირთვა
ხდება file_info სტრუქტურის მიხედვით. `mmap`-ისას ფაილი ხელახლა იხსნება, რათა `munmap`-მდე
არ მოხდეს ფაილის დახურვა.

memory unmapping-ისას უბრალოდ გადავუყვებით და თითოეულ supplemental page-ს ვამოწმებთ და თუ `frame`
NULL-ი არაა ფაილში იწერება, წინააღმდეგ შემთხვევაში თუ ფრეიმი არ აქვს, ანუ ამ `v_addr` მისამართზე 
მიმართვა ან არა მომხდარა, ან თუ მოხდა eviction-ისას უკვე ფაილში ჩაიწერა.

mmap სტრუქტურა ინახება თითოეული პროცესის thread სტრუქტურის hash table-ში და დაჰეშილია `mapid_t` 
ცვლადით.


---

## Swap

##### Data Structures

swap.h
```c
/* Index of a slot in swap. */
typedef long swap_slot_t;

void swap_init(void);
swap_slot_t swap_out(void*);
void swap_in(swap_slot_t, void*);
```

##### Solution

swap-ისთვის შევქმენით მარტივი ინტერფეისი. `swap_in()` და `swap_out()` ფუნქციები აბსტრაგირებას 
უკეთებს დისკთან მუშაობას.

ჩვენი `eviction()` ფუნქცია წარმოადგენს მარტივ `second chance` ალგორითმს, და იძახება `frame_allocate()` 
ფუნქციაში, როდესაც user pool-ში აღარაა თავისუფალი ფეიჯები. frame table-იდან ხდება რომელიღაც `frame`-ის 
გამოთავისუფლება, ანუ მისი პეიჯი ინახება swap მეხსიერებაში, ან თუ ამ პეიჯზე ფაილის ნაწილია დამეპილი და 
dirty ბიტი აქვს დასმული პირდაპირ ფაილში იწერება. 

eviction-ისას frame table დალოქილია. ასევე `page_load()`-ისას ხდება ფრეიმის „დაპინვა“, რათა არ
მოხდეს მისი სვაპში გატანა და შესაბამისად ინფორმაციის დაკარგვა.

