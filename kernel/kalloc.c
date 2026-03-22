// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmemcpu[NCPU];


void
kinit()
{
  char *p;
  void *pa_end, *pa_start;
  struct run *r;
  char* curr_start = end;
  uint64 total   = PHYSTOP - (uint64)PGROUNDUP((uint64)end);
  uint64 mem_cap = PGROUNDDOWN(total / NCPU);   // bytes per CPU, page-aligned

  for(int i = 0; i < NCPU; i++)
  {
    initlock(&(kmemcpu[i].lock), "kmemcpu");
    // freerange(curr_start, (void *)(curr_start + mem_cap));
    pa_start = curr_start;
    pa_end = (void *)(curr_start + mem_cap);
    p = (char*)PGROUNDUP((uint64)pa_start);
    for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
      // kfree(p);
      {
      if(((uint64)p % PGSIZE) != 0 || (char*)p < end || (uint64)p >= PHYSTOP)
        panic("kfree");

      memset(p, 1, PGSIZE);

      r = (struct run*)p;

      acquire(&(kmemcpu[i].lock));
      r->next = kmemcpu[i].freelist;
      kmemcpu[i].freelist = r;
      release(&(kmemcpu[i].lock));
      }

    curr_start = (void *)(curr_start + mem_cap);
  }
  freerange(curr_start, (void *)PHYSTOP);
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
  int cpu_id = cpuid();
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&(kmemcpu[cpu_id].lock));
  r->next = kmemcpu[cpu_id].freelist;
  kmemcpu[cpu_id].freelist = r;
  release(&(kmemcpu[cpu_id].lock));
}

struct run* steal_mem(int cpu_id)
{
  struct run *r;

  for(int i = 0; i < NCPU; i++)
  {
    if(i == cpu_id)
      continue;
    acquire(&(kmemcpu[i].lock));
    r = kmemcpu[i].freelist;
    if(r)
    {
      struct run* slow = r;
      struct run* fast = r;
      while(fast && fast->next)
      {
        slow = slow->next;
        fast = fast->next->next;
      }
      kmemcpu[i].freelist = slow->next;
      slow->next = 0;
      release(&(kmemcpu[i].lock));
      return r;
    }
    release(&(kmemcpu[i].lock));
  }
  return 0;
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu_id = cpuid();

  acquire(&(kmemcpu[cpu_id].lock));
  r = kmemcpu[cpu_id].freelist;
  if(r)
    kmemcpu[cpu_id].freelist = r->next;
  else {
    release(&(kmemcpu[cpu_id].lock));
    r = steal_mem(cpu_id);
    acquire(&(kmemcpu[cpu_id].lock));
    if(r)
      kmemcpu[cpu_id].freelist = r->next;
  }
  release(&(kmemcpu[cpu_id].lock));

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
