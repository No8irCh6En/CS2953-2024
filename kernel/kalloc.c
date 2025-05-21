// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PA2PGID(p) (((p)-KERNBASE)/PGSIZE)
#define PG_SIZE_REF PA2PGID(PHYSTOP)

int page_ref[PG_SIZE_REF];
struct spinlock page_ref_lock;

#define PA2PGREF(p) page_ref[PA2PGID((uint64)p)]

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&(kmem[i].lock), "kmem");
  initlock(&page_ref_lock, "page_ref_lock");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.

  acquire(&page_ref_lock);

  if(--(PA2PGREF(pa)) <= 0){

    r = (struct run*)pa;

    memset(pa, 1, PGSIZE);

    push_off();
    int cpu_id = cpuid();

    acquire(&(kmem[cpu_id].lock));
    r->next = kmem[cpu_id].freelist;
    kmem[cpu_id].freelist = r;
    release(&kmem[cpu_id].lock);

    pop_off();

  }
  release(&page_ref_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu_id = cpuid();

  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;
  if(r){
    kmem[cpu_id].freelist = r->next;
    release(&kmem[cpu_id].lock);
  }
  else{
    release(&(kmem[cpu_id].lock));
    for (int i = 0;i < NCPU; i++){
      if (i == cpu_id) continue;
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if(r){
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }


  pop_off();

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk

    PA2PGREF(r) = 1;
  }
  return (void*)r;
}


uint64 count_freemem(void){
  struct run *r;
  int cnt = 0;
  for (int i = 0; i < NCPU; i++){
    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    while(r){
      cnt++;
      r = r->next;
    }
    release(&kmem[i].lock);
  }

  return cnt * PGSIZE;
}

void add1ref(void* pa){
    acquire(&page_ref_lock);
    PA2PGREF(pa)++;
    release(&page_ref_lock);
}

void* kcopy_or_give(void* pa){
    acquire(&page_ref_lock);
    if (PA2PGREF(pa) > 1){
        PA2PGREF(pa)--;
        uint64 new_pg = (uint64)kalloc();
        if(new_pg == 0){
            release(&page_ref_lock);
            return 0;
        }
        memmove((void*)new_pg, pa, PGSIZE);
        release(&page_ref_lock);
        return (void*)new_pg;
    }
    release(&page_ref_lock);
    return pa;
}