#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "defs.h"
#include "fcntl.h"
#include "file.h"


void vma_fork(struct proc *p, struct proc *np) {
  acquire(&p->tshared->tlock);
  for (int i = 0; i < MAX_VMA; i++) {
    np->tshared->vm_areas[i] = p->tshared->vm_areas[i];
    if(!np->tshared->vm_areas[i].used) continue;
    if(np->tshared->vm_areas[i].file) {
      filedup(np->tshared->vm_areas[i].file);
    }
    if (np->tshared->vm_areas[i].type == EXEC) {
      if (np->tshared->vm_areas[i].ip) idup(np->tshared->vm_areas[i].ip);
    }
  }
  release(&p->tshared->tlock);
}

void vma_exec_clear(struct proc *p) {
  acquire(&p->tshared->tlock);
  for (int i = FIX_START_ADDR; i < MAX_VMA; i++) {
    if(!p->tshared->vm_areas[i].used) continue;
    if (p->tshared->vm_areas[i].type == EXEC) {
      if (p->tshared->vm_areas[i].ip) iput(p->tshared->vm_areas[i].ip);
      p->tshared->vm_areas[i].used = 0;
      //uvmunmap(p->pagetable, p->tshared->vm_areas[i].addr, PGROUNDUP(p->tshared->vm_areas[i].length)/PGSIZE, 1);
    } else if (p->tshared->vm_areas[i].type == HEAP) {
      p->tshared->vm_areas[i].used = 0;
    }
  }
  release(&p->tshared->tlock);
}

int vma_handle(struct proc *p, uint64 va) {
  if (va > MAXVA) {
    return -1;
  }
  va = PGROUNDDOWN(va);
  pte_t *pte = walk(p->pagetable, va, 0);
  if (pte && (*pte & PTE_PG)) {
    return pagein(pte, &p->tshared->slock);
  }
  int ret = 0;
  acquiresleep(&p->tshared->slock);
  acquire(&p->tshared->tlock);
  for (int i = 0; i < MAX_VMA; i++) {
    struct vma *vma = &p->tshared->vm_areas[i];
    
    if (!vma->used || vma->addr > va || va >= vma->addr + vma->length) continue;
    // check thread already created vma
    if ((vma->flags & MAP_SUPPG) && walkaddr(p->pagetable, SUPPGROUNDDOWN(va)) != 0) {
      ret = 1;
      goto success;
    }
    else if (walkaddr(p->pagetable, va)) goto success;

    uint64 mem = 0;
    int private = (vma->flags & MAP_PRIVATE);
    int pgsize = PGSIZE;
    if (private) {
      // Allocate a new page if private
      char *tmp;
      if (vma->flags & MAP_SUPPG) {
        if ((tmp = kalloc_suppage()) == 0) {
          goto err;
        }
        ret = 1;
        pgsize = SUPPGSIZE;
        va = SUPPGROUNDDOWN(va);
      } else {
        release(&p->tshared->tlock);
        if ((tmp = kalloc()) == 0) {
          acquire(&p->tshared->tlock);
          goto err;
        }
        acquire(&p->tshared->tlock);
      }
      memset(tmp, 0, pgsize);
      mem = (uint64) tmp;
    }

    // Read file content into the new page
    struct inode *ip = vma->ip;
    if (ip) {
      idup(ip);
      release(&p->tshared->tlock);
      ilock(ip);
      
      if (private) {
        int n;
        int sz = vma->addr + vma->filesz - va;
        if(sz < pgsize)
          n = sz;
        else
          n = pgsize;
        if (readi(ip, 0, (uint64)mem, vma->offset + va - vma->addr, n) < 0) {
          iunlockput(ip);
          kfree((void *)mem);
          acquire(&p->tshared->tlock);
          goto err;
        }
      } else if (!(mem = readblock(ip, vma->offset + va - vma->addr))) {
        iunlockput(ip);
        acquire(&p->tshared->tlock);
        goto err;
      }
      iunlockput(ip);
      acquire(&p->tshared->tlock);
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
    
    release(&p->tshared->tlock);
    if(mappages(p->pagetable, va, pgsize, mem, perm) != 0){
      if (private) IS_SUPPG(mem) ? kfree_suppage((void *)mem) : kfree((void *)mem);
      else if (vma->file) bunpin2(mem);
      acquire(&p->tshared->tlock);
      goto err;
    }
success:
    releasesleep(&p->tshared->slock);  
    return ret;  // Page fault handled successfully
  }
err:
  release(&p->tshared->tlock);
  releasesleep(&p->tshared->slock);
  return -1;  // No corresponding VMA found, or other error
}

uint64 setup_vma(struct proc *p, int i, uint64 addr, size_t len, int prot, int flags, 
                struct file *f, struct inode *ip, off_t offset, size_t filesz, enum vmatype type)
{
  if (f && (f->type != FD_INODE || !filedup(f))) {
    return -1;
  }
  p->tshared->vm_areas[i].used = 1;
  p->tshared->vm_areas[i].addr = addr; 
  p->tshared->vm_areas[i].length = len;
  p->tshared->vm_areas[i].prot = prot;
  p->tshared->vm_areas[i].flags = flags;
  p->tshared->vm_areas[i].file = f;
  p->tshared->vm_areas[i].ip = ip;
  p->tshared->vm_areas[i].offset = offset;
  p->tshared->vm_areas[i].filesz = filesz;
  p->tshared->vm_areas[i].type = type;
  return p->tshared->vm_areas[i].addr; 
}
// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   mmapped files
//   USYSCALL
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
uint64 vma_create(uint64 addr, size_t len, int prot, int flags, struct file *f, struct inode *ip, off_t offset, size_t filesz, enum vmatype type)
{
  if(f && ((!f->readable && (prot & (PROT_READ)))
     || (!f->writable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE))))
    return -1;

  uint64 ret = -1;  
  struct proc *p = myproc();
  if (type != HEAP)
    acquire(&p->tshared->tlock);
  int suppg = (flags & MAP_SUPPG);
  if (type == DYNAMIC) {
    uint64 end = suppg ? SUPPGROUNDDOWN(USYSCALL) : PGROUNDDOWN(USYSCALL);  // Start searching from address below USYSCALL
    for (int i = 0; i < MAX_DYN_VMA; ) {
      if (p->tshared->vm_areas[i].used) {
        end = suppg ? SUPPGROUNDDOWN(p->tshared->vm_areas[i].addr) : p->tshared->vm_areas[i].addr;
        
        i++;
        continue;  
      }
      int j = i + 1;
      for (; j < MAX_DYN_VMA; j++) {
        if (p->tshared->vm_areas[j].used) break;
      }
      int next_end;
      if (suppg) {
        next_end = (j == MAX_DYN_VMA) ? SUPPGROUNDUP(p->tshared->sz) : SUPPGROUNDUP(p->tshared->vm_areas[j].addr + p->tshared->vm_areas[j].length);
      } else {
        next_end = (j == MAX_DYN_VMA) ? PGROUNDUP(p->tshared->sz) : PGROUNDUP(p->tshared->vm_areas[j].addr + p->tshared->vm_areas[j].length);
      }
      if (end - next_end >= len) {
        ret = setup_vma(p, i, suppg ? SUPPGROUNDDOWN(end - len) : PGROUNDDOWN(end - len), len, prot, flags, f, ip, offset, filesz, type); 
        goto ending;
      }
      i = j;
    }
  } else if (type == HEAP) {
    int empty = -1;
    for (int i = 0; i < MAX_FIX_VMA; i++) {
      int j = FIX_START_ADDR + i;
      struct vma *v = &p->tshared->vm_areas[j];
      if (v->used && v->type == HEAP) {
        if (addr != v->addr + v->length) panic("update_heap_vma");
        v->length += len;
        if (v->length <= 0) {
          v->used = 0;
        }
        ret = addr;
        goto ending;
      } else if (!v->used && empty == -1) {
        empty = j;
      }
    }
    if (empty != -1 && len > 0) {
      ret = setup_vma(p, empty, addr, len, prot, flags, f, ip, offset, filesz, type); 
      goto ending;
    }
  } else {
    for (int i = 0; i < MAX_VMA; i++) {
      if (!p->tshared->vm_areas[i].used) continue;
      if (p->tshared->vm_areas[i].addr <= addr && addr < p->tshared->vm_areas[i].addr + p->tshared->vm_areas[i].length) {
        goto ending;
      }
    }
    for (int i = 0; i < MAX_FIX_VMA; i++) {
      int j = FIX_START_ADDR + i;
      if (p->tshared->vm_areas[j].used) continue;
      if (ip) idup(ip);
      ret = setup_vma(p, j, addr, len, prot, flags, f, ip, offset, filesz, type);
      goto ending;
    }
  }
ending:
  if (type != HEAP)
    release(&p->tshared->tlock);
  return ret;  // No space found
}

uint64 writeback(uint64 addr, size_t len, struct proc *p, struct vma *v)
{
  if (addr % PGSIZE) panic("writeback");
  uint64 end = addr + len, off = 0;
  release(&p->tshared->tlock);
  for(;addr < end; addr += PGSIZE, off += PGSIZE)
  {
    pte_t *pte = walk(p->pagetable, addr, 0);
    if ((*pte & PTE_D) && (*pte & PTE_W)) {
      begin_op();
      ilock(v->file->ip);
      if (writei(v->file->ip, 1, addr, off, PGSIZE > end - addr ? (end - addr) : PGSIZE) < 0) {
        iunlock(v->file->ip);
        end_op();
        acquire(&p->tshared->tlock);
        return -1;
      }
      iunlock(v->file->ip);
      end_op();
    }
  }
  acquire(&p->tshared->tlock);
  return 0;
}

uint64 munmap(uint64 addr, size_t len)
{
  if (addr % PGSIZE != 0) return -1;
  len = PGROUNDDOWN(len);
  if (len == 0) return 0;

  struct proc *p = myproc();
  acquiresleep(&p->tshared->slock);
  acquire(&p->tshared->tlock);
  for (int i = 0; i < MAX_VMA; i++) {
    struct vma *v = &p->tshared->vm_areas[i];
    if (!v->used || v->type == EXEC || v->type == HEAP) continue;
    if (v->addr <= addr && addr < v->addr + v->length) {
      if (v->flags == MAP_SHARED && v->file && writeback(addr, len, p, v) < 0)
        goto err;
      
      if (v->length == len) {
        v->used = 0;
        if (v->file) {
          release(&p->tshared->tlock);
          fileclose(v->file);
          acquire(&p->tshared->tlock);
        }
      } else {
        if (v->addr == addr) v->addr += len;
        v->length -= len;
      }

      uvmunmap(p->pagetable, addr, PGROUNDUP(len)/PGSIZE, (v->flags & MAP_SHARED) ? FREE_BCACHE : 1);
      release(&p->tshared->tlock);
      releasesleep(&p->tshared->slock);
      return 0;
    }
  }
err:  
  release(&p->tshared->tlock);
  releasesleep(&p->tshared->slock);
  return -1;
}

void mmap_clean(struct proc *p)
{
  if (p->isthread) return;
  acquiresleep(&p->tshared->slock);
  acquire(&p->tshared->tlock);
  for (int i = 0; i < MAX_VMA; i++) {
    struct vma *v = &p->tshared->vm_areas[i];
    if (!v->used) continue;
    if (v->flags == MAP_SHARED && writeback(v->addr, v->length, p, v) < 0)
      panic("mmap clean");
    uvmunmap(p->pagetable, v->addr , PGROUNDUP(v->length)/PGSIZE, (v->flags & MAP_SHARED) ? FREE_BCACHE : 1);
    v->used = 0;
    if (v->file) {
      release(&p->tshared->tlock);
      fileclose(v->file);
      acquire(&p->tshared->tlock);
    }
    if (v->type == EXEC && v->ip) 
      iput(v->ip); 
  }
  release(&p->tshared->tlock);
  releasesleep(&p->tshared->slock);
}

int space_enough(struct proc *p, int n)
{
  if (p->tshared->sz + n < 0) return 0;
  for (int i = 0; i < MAX_FIX_VMA; i++) {
    int j = FIX_START_ADDR + i;
    if (!p->tshared->vm_areas[j].used || p->tshared->vm_areas[j].type == HEAP) continue;
    uint64 addr = p->tshared->sz + n;
    if (p->tshared->vm_areas[j].addr <= addr && addr < p->tshared->vm_areas[j].addr + p->tshared->vm_areas[j].length) return 0;
  }
  for (int i = MAX_DYN_VMA - 1; i >= 0; i--) {
    if (!p->tshared->vm_areas[i].used) continue;
    if (p->tshared->sz + n > p->tshared->vm_areas[i].addr) return 0;
    break;
  }
  return p->tshared->sz + n <= USYSCALL;
}

uint64 update_heap_vma(struct proc *p, uint64 addr, int n)
{
  if (vma_create(addr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE, 0, 0, 0, 0, HEAP) < 0) return -1;
  p->tshared->sz += n;
  return addr;
}

uint64 vma_sbrk(struct proc *p, int n)
{
  acquire(&p->tshared->tlock);
  uint64 addr = p->tshared->sz;
  if (!space_enough(p, n)) {
    release(&p->tshared->tlock);
    return -1;
  }
  if(n < 0){
    uvmdealloc(p->pagetable, p->tshared->sz, p->tshared->sz + n);
  } else {
    saved_page((PGROUNDUP(p->tshared->sz + n) - PGROUNDUP(p->tshared->sz)) / PGSIZE);
  }
  update_heap_vma(p, addr, n);
  release(&p->tshared->tlock);
  return addr;
}