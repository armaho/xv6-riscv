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

static uint8 *refcount;

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
  for (int i = 0; i < NCPU; i++) {
    char name[7] = "kmem- ";
    name[5] = 'a' + i;

    initlock(&kmem[i].lock, name);
  }

  uint refcountsize = PGROUNDUP((PHYSTOP - KERNBASE) / PGSIZE);
  refcount = (uint8 *)end;
  memset(refcount, 1, refcountsize);

  freerange(end + refcountsize, (void*)PHYSTOP);
}

uint8 *krefcount(void *pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("krefcount");

  int idx = ((uint64)pa - KERNBASE) / PGSIZE;
  return &refcount[idx];
}

void kaddref(void *pa) {
  uint8 *rc = krefcount(pa);
  (*rc)++;
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Decrement the ref count of physical page and 
// if possible, free the page of physical memory 
// pointed at by pa, which normally should have 
// been returned by a call to kalloc().  
// (The exception is when initializing the allocator; 
// see kinit above.)
void
kfree(void *pa)
{
  uint8 *rc = krefcount(pa);
  (*rc)--;

  if (*rc < 0) panic("kremref: invalid rc");
  if (*rc > 0) return;
  
  struct run *r;

  // Fill with junk.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpu = cpuid();
  pop_off();

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
}

void *
kborrow(void) {
  struct run *r;

  for (int i = 0; i < NCPU; i++) {
    acquire(&kmem[i].lock);

    if ((r = kmem[i].freelist)) {
      kmem[i].freelist = r->next;
      release(&kmem[i].lock);
      return r;
    }

    release(&kmem[i].lock);
  }

  return 0;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int cpu = cpuid();
  pop_off();

  struct run *r;

  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  if (!r) {
    r = kborrow();
  }

  if(r) {
    kaddref((char *)r);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }

  return (void*)r;
}
