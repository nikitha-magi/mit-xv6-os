#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"


void
spagefault(void)
{
  struct proc *p = myproc();
  uint64 va = PGROUNDDOWN(r_stval());
  pte_t *pte = walk(p->pagetable, va, 0);
  
  if(pte == 0 || (*pte & PTE_V) == 0){
    setkilled(p);
    return;
  }

  uint flags = PTE_FLAGS(*pte);

  if(!(flags & PTE_COW)){   // not a COW page — real fault
    setkilled(p);
    return;
  }

  uint64 pa = PTE2PA(*pte);
  char *mem = kalloc();
  if(mem == 0){
    setkilled(p);
    return;
  }
  memmove(mem, (char*)pa, PGSIZE);  // copy old page
  refdesc((void *)pa);
//   refdec((void*)pa);                // decrement old page ref count

  flags = (flags | PTE_W) & ~PTE_COW;  // writable, no longer COW

  // unmap old, map new
  uvmunmap(p->pagetable, va, 1, 0);
  mappages(p->pagetable, va, PGSIZE, (uint64)mem, flags);
//   printf("Handled\n");
}