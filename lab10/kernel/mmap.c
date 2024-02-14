#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "defs.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "file.h"


void vma_fork(struct proc *p, struct proc *np) {
  for (int i = 0; i < MAX_VMA; i++) {
    np->vm_areas[i] = p->vm_areas[i];
    if(!np->vm_areas[i].used) continue;
    if(np->vm_areas[i].file) {
      filedup(np->vm_areas[i].file);
    }
    if (np->vm_areas[i].type == EXEC) {
      if (np->vm_areas[i].ip) idup(np->vm_areas[i].ip);
    }
  }
}

void vma_exec_clear(struct proc *p) {
  for (int i = FIX_START_ADDR; i < MAX_VMA; i++) {
    if(!p->vm_areas[i].used) continue;
    if (p->vm_areas[i].type == EXEC) {
      if (p->vm_areas[i].ip) iput(p->vm_areas[i].ip);
      p->vm_areas[i].used = 0;
      //uvmunmap(p->pagetable, p->vm_areas[i].addr, PGROUNDUP(p->vm_areas[i].length)/PGSIZE, 1);
    } else if (p->vm_areas[i].type == HEAP) {
      p->vm_areas[i].used = 0;
    }
  }
}

int vma_handle(struct proc *p, uint64 va) {
  if (va > MAXVA) return -1;
  va = PGROUNDDOWN(va);
  pte_t *pte = walk(p->pagetable, va, 0);
  if (pte && (*pte & PTE_PG)) {
    return pagein(pte);
  }
  for (int i = 0; i < MAX_VMA; i++) {
    struct vma *vma = &p->vm_areas[i];
    if (!vma->used || vma->addr > va || va >= vma->addr + vma->length) continue;
    
    uint64 mem = 0;
    int private = (vma->flags & MAP_PRIVATE);
    if (private) {
      // Allocate a new page if private
      char *tmp;
      if ((tmp = kalloc()) == 0) return -1;
      memset(tmp, 0, PGSIZE);
      mem = (uint64) tmp;
    }

    // Read file content into the new page
    struct inode *ip = vma->ip;
    if (ip) {
      idup(ip);
      ilock(ip);
      
      if (private) {
        int n;
        int sz = vma->addr + vma->filesz - va;
        if(sz < PGSIZE)
          n = sz;
        else
          n = PGSIZE;
        if (readi(ip, 0, (uint64)mem, vma->offset + va - vma->addr, n) < 0) {
          
          iunlockput(ip);
          kfree((void *)mem);
          return -1;
        }
      } else if (!(mem = readblock(ip, vma->offset + va - vma->addr))) {
        iunlockput(ip);
        return -1;
      }
      iunlockput(ip);
    }
    // printf("%d %d %p %d\n", p->pid, mycpu()->noff, va, vma->type); 
    if (!mem) panic("vma_handle");
    // Map the new page at the faulting address
    int perm = PTE_U;
    if(vma->prot & PROT_READ)
      perm |= PTE_R;
    if(vma->prot & PROT_WRITE)
      perm |= PTE_W;
    if(vma->prot & PROT_EXEC)
      perm |= PTE_X;
    
    if(mappages(p->pagetable, va, PGSIZE, mem, perm) != 0){
      if (private) kfree((void *)mem);
      else if (vma->file) bunpin2(mem);
      return -1;
    }
    return 0;  // Page fault handled successfully
  }

  return -1;  // No corresponding VMA found, or other error
}

uint64 setup_vma(struct proc *p, int i, uint64 addr, size_t len, int prot, int flags, 
                struct file *f, struct inode *ip, off_t offset, size_t filesz, enum vmatype type)
{
  if (f && (f->type != FD_INODE || !filedup(f))) {
    return -1;
  }
  p->vm_areas[i].used = 1;
  p->vm_areas[i].addr = addr; 
  p->vm_areas[i].length = len;
  p->vm_areas[i].prot = prot;
  p->vm_areas[i].flags = flags;
  p->vm_areas[i].file = f;
  p->vm_areas[i].ip = ip;
  p->vm_areas[i].offset = offset;
  p->vm_areas[i].filesz = filesz;
  p->vm_areas[i].type = type;
  return p->vm_areas[i].addr; 
}
// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   mmapped files
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
uint64 vma_create(uint64 addr, size_t len, int prot, int flags, struct file *f, struct inode *ip, off_t offset, size_t filesz, enum vmatype type)
{
  if(f && ((!f->readable && (prot & (PROT_READ)))
     || (!f->writable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE))))
    return -1;

  struct proc *p = myproc();
  
  if (type == DYNAMIC) {
    uint64 end = PGROUNDDOWN(TRAPFRAME);  // Start searching from address below TRAPFRAME
    for (int i = 0; i < MAX_DYN_VMA; ) {
      if (p->vm_areas[i].used) {
        end = p->vm_areas[i].addr;
        i++;
        continue;  
      }
      int j = i + 1;
      for (; j < MAX_DYN_VMA; j++) {
        if (p->vm_areas[j].used) break;
      }
      int next_end = (j == MAX_DYN_VMA) ? PGROUNDUP(p->sz) : PGROUNDUP(p->vm_areas[j].addr + p->vm_areas[j].length);
      if (end - next_end >= len) {
        return setup_vma(p, i, PGROUNDDOWN(end - len), len, prot, flags, f, ip, offset, filesz, type); 
      }
      i = j;
    }
  } else if (type == HEAP) {
    int empty = -1;
    for (int i = 0; i < MAX_FIX_VMA; i++) {
      int j = FIX_START_ADDR + i;
      struct vma *v = &p->vm_areas[j];
      if (v->used && v->type == HEAP) {
        if (addr != v->addr + v->length) panic("update_heap_vma");
        v->length += len;
        if (v->length <= 0) {
          v->used = 0;
        }
        return addr;
      } else if (!v->used && empty == -1) {
        empty = j;
      }
    }
    if (empty != -1 && len > 0) {
      return setup_vma(p, empty, addr, len, prot, flags, f, ip, offset, filesz, type); 
    }
  } else {
    for (int i = 0; i < MAX_VMA; i++) {
      if (!p->vm_areas[i].used) continue;
      if (p->vm_areas[i].addr <= addr && addr < p->vm_areas[i].addr + p->vm_areas[i].length) {
        return -1;
      }
    }
    for (int i = 0; i < MAX_FIX_VMA; i++) {
      int j = FIX_START_ADDR + i;
      if (p->vm_areas[j].used) continue;
      if (ip) idup(ip);
      return setup_vma(p, j, addr, len, prot, flags, f, ip, offset, filesz, type);
    }
  }

  return -1;  // No space found
}

uint64 writeback(uint64 addr, size_t len, struct proc *p, struct vma *v)
{
  if (addr % PGSIZE) panic("writeback");
  uint64 end = addr + len, off = 0;
  
  for(;addr < end; addr += PGSIZE, off += PGSIZE)
  {
    pte_t *pte = walk(p->pagetable, addr, 0);
    if ((*pte & PTE_D) && (*pte & PTE_W)) {
      begin_op();
      ilock(v->file->ip);
      if (writei(v->file->ip, 1, addr, off, PGSIZE > end - addr ? (end - addr) : PGSIZE) < 0) {
        iunlock(v->file->ip);
        end_op();
        return -1;
      }
      iunlock(v->file->ip);
      end_op();
    }
  }
  return 0;
}

uint64 munmap(uint64 addr, size_t len)
{
  if (addr % PGSIZE != 0) return -1;
  len = PGROUNDDOWN(len);
  if (len == 0) return 0;

  struct proc *p = myproc();
  for (int i = 0; i < MAX_VMA; i++) {
    struct vma *v = &p->vm_areas[i];
    if (!v->used || v->type == EXEC || v->type == HEAP) continue;
    if (v->addr <= addr && addr < v->addr + v->length) {
      if (v->flags == MAP_SHARED && v->file && writeback(addr, len, p, v) < 0)
        return -1;
      
      if (v->length == len) {
        v->used = 0;
        if (v->file)
          fileclose(v->file);
      } else {
        if (v->addr == addr) v->addr += len;
        v->length -= len;
      }

      uvmunmap(p->pagetable, addr, PGROUNDUP(len)/PGSIZE, (v->flags & MAP_SHARED) ? FREE_BCACHE : 1);
      return 0;
    }
  }
  return -1;
}

void mmap_clean(struct proc *p)
{
  for (int i = 0; i < MAX_VMA; i++) {
    struct vma *v = &p->vm_areas[i];
    if (!v->used) continue;
    if (v->flags == MAP_SHARED && writeback(v->addr, v->length, p, v) < 0)
      panic("mmap clean");
    uvmunmap(p->pagetable, v->addr , PGROUNDUP(v->length)/PGSIZE, (v->flags & MAP_SHARED) ? FREE_BCACHE : 1);
    v->used = 0;
    if (v->file)
      fileclose(v->file);
    if (v->type == EXEC && v->ip) 
      iput(v->ip); 
  }
}

int space_enough(struct proc *p, int n)
{
  if (p->sz + n < 0) return 0;
  for (int i = 0; i < MAX_FIX_VMA; i++) {
    int j = FIX_START_ADDR + i;
    if (!p->vm_areas[j].used || p->vm_areas[j].type == HEAP) continue;
    uint64 addr = p->sz + n;
    if (p->vm_areas[j].addr <= addr && addr < p->vm_areas[j].addr + p->vm_areas[j].length) return 0;
  }
  for (int i = MAX_DYN_VMA - 1; i >= 0; i--) {
    if (!p->vm_areas[i].used) continue;
    if (p->sz + n > p->vm_areas[i].addr) return 0;
    break;
  }
  return p->sz + n <= TRAPFRAME;
}

uint64 update_heap_vma(struct proc *p, uint64 addr, int n)
{
  if (vma_create(addr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE, 0, 0, 0, 0, HEAP) < 0) return -1;
  p->sz += n;
  return addr;
}

uint64 vma_sbrk(struct proc *p, int n)
{
  uint64 addr = p->sz;
  if (!space_enough(p, n)) {
    return -1;
  }
  if(n < 0){
    uvmdealloc(p->pagetable, p->sz, p->sz + n);
  } else {
    saved_page((PGROUNDUP(p->sz + n) - PGROUNDUP(p->sz)) / PGSIZE);
  }
  update_heap_vma(p, addr, n);

  return addr;
}