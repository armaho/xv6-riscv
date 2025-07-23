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
void freemegarange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem, kmegamem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kmegamem.lock, "kmegamem");
  freerange(end, (void*)(PHYSTOP - MPGCNT * MPGSIZE));
  freemegarange((void*)(PHYSTOP - MPGCNT * MPGSIZE), (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void
freemegarange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)MPGROUNDUP((uint64)pa_start);
  for(; p + MPGSIZE <= (char*)pa_end; p += MPGSIZE)
    kmegafree(p);
}

void
kfreelevel(void *pa, uint level) {
  switch (level) {
    case 0: kfree(pa); return;
    case 1: kmegafree(pa); return;
    default: panic("kfreelevel: invalid level");
  }
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
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Like kfree, but for megapages
void
kmegafree(void *pa)
{
  struct run *r;

  if(((uint64)pa % MPGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) {
    printf("%lu\n", (uint64)pa % MPGSIZE);
    panic("kmegafree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, MPGSIZE);

  r = (struct run*)pa;

  acquire(&kmegamem.lock);
  r->next = kmegamem.freelist;
  kmegamem.freelist = r;
  release(&kmegamem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Like kalloc, but for megapages
void *
kmegaalloc(void)
{
  struct run *r;

  acquire(&kmegamem.lock);
  r = kmegamem.freelist;
  if(r)
    kmegamem.freelist = r->next;
  release(&kmegamem.lock);

  if(r)
    memset((char*)r, 5, MPGSIZE); // fill with junk
  return (void*)r;
}

void *
kalloclevel(uint level) {
  switch (level) {
    case 0: return kalloc();
    case 1: return kmegaalloc();
    default: panic("kfreelevel: invalid level");
  }
}
