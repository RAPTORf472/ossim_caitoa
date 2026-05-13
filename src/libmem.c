/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fifo_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  if (mm == NULL || mm->mmap == NULL)
    return -1;
  if (rg_elmt == NULL || rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end   = rgnode.rg_end;
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  //get_free_vmrg_area FAILED — expand VMA limit

  addr_t aligned_sz;
#ifdef MM64
  aligned_sz = ((size + PAGING64_PAGESZ - 1) / PAGING64_PAGESZ) * PAGING64_PAGESZ;
#else
  aligned_sz = PAGING_PAGE_ALIGNSZ(size);
#endif

  addr_t old_sbrk = cur_vma->sbrk;

  //SYSCALL 17 
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = aligned_sz;   

  if (_syscall(caller->krnl, caller->pid, 17, &regs) < 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  //write in symrgtbl
  caller->krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->krnl->mm->symrgtbl[rgid].rg_end   = old_sbrk + size;
  *alloc_addr = old_sbrk;

  //remaining after alignmentn put into freelist
  if (aligned_sz > size) {
    struct vm_rg_struct *free_rg = malloc(sizeof(struct vm_rg_struct));
    free_rg->rg_start = old_sbrk + size;
    free_rg->rg_end   = old_sbrk + aligned_sz;
    free_rg->rg_next  = NULL;   /* fix 3: khởi tạo rg_next */
    enlist_vm_freerg_list(caller->krnl->mm, free_rg);
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
  proc->regs[reg_index] = addr;
  printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, addr, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  addr_t old_addr =
    proc->krnl->mm->symrgtbl[reg_index].rg_start;

  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
  proc->regs[reg_index] = 0;

printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, old_addr,-1); // print max TBL
#endif
#endif
  return 0;//val;
} 

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */

int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  #define PAGING_PAGE_SWAPPED(pte) ((pte) & PAGING_PTE_SWAPPED_MASK)

  uint32_t pte = pte_get_entry(caller, pgn);

  //Provided hints assume that RAM is always full, which is kinda wrong
  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn, swpfpn;
    addr_t vicfpn;
    addr_t vicpte;
//  struct sc_regs regs;
    /* TODO Initialize the target frame storing our variable */
    addr_t tgtfpn; 
    
    //Check if RAM is full
    if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == -1) {
      //RAM is full, swap out a page
      /* TODO: Play with your paging theory here */
      
      pthread_mutex_lock(&fifo_lock);
      /* Find victim page */
      if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
      {
        pthread_mutex_unlock(&fifo_lock);
        return -1;
      }
      pthread_mutex_unlock(&fifo_lock);

      /* Get free frame in MEMSWP */
      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
      {
        return -1;
      }

      /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/
      vicpte = pte_get_entry(caller, vicpgn);
      vicfpn = PAGING_FPN(vicpte);
      tgtfpn = vicfpn;

      /* TODO copy victim frame to swap 
      * SWP(vicfpn <--> swpfpn)
      * SYSCALL 1 sys_memmap
      */
      __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);

      /* Update page table */
      //pte_set_swap(...);
      pte_set_swap(caller, vicpgn, 0, swpfpn);

    }

    if (PAGING_PAGE_SWAPPED(pte)) {
       addr_t target_swpfpn = PAGING_SWP(pte); 
       
       __swap_cp_page(caller->krnl->active_mswp, target_swpfpn, 
                      caller->krnl->mram, tgtfpn);
                      
       MEMPHY_put_freefp(caller->krnl->active_mswp, target_swpfpn);
    }
       
    /* Update its online status of the target page */
    //pte_set_fpn(...);
    pte_set_fpn(caller, pgn, tgtfpn);

    pthread_mutex_lock(&fifo_lock);
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
    pthread_mutex_unlock(&fifo_lock);
    
  }

  *fpn = PAGING_FPN(pte_get_entry(caller,pgn));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* TODO 
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ 
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  MEMPHY_read(caller->krnl->mram, phyaddr, data);
  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int physAddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* TODO 
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE 
   * SYSCALL 17 sys_memmap
   */

  MEMPHY_write(caller->krnl->mram, physAddr,value);

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  /* TODO Invalid memory identify */
  if(currg == NULL || cur_vma ==NULL){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start >= currg->rg_end ){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start+offset >= currg->rg_end){
    pthread_mutex_unlock(&mmvm_lock);
    return -1; //offset outside region bound
  }

  pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
printf("%s:%d\n",__func__,__LINE__);
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, proc->regs[source] + offset, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start>=currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1; //not alloc region
  }
  if(currg->rg_start+offset>= currg->rg_end){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  if(size == 0) {
    return -1;
  }
  if(reg_index<0|| reg_index>= PAGING_MAX_SYMTBL_SZ) {
    return -1;
  }
  addr_t  addr;
  int val = __kmalloc(caller, -1, reg_index, size, &addr);
  if(val!=0){
    return -1;
  }
  /* TODO: provide OS kmem allocation validation
   */

  return val;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */
  struct krnl_t *krnl = caller->krnl;
  addr_t aligned_sz =((size + PAGING64_PAGESZ - 1)/ PAGING64_PAGESZ)* PAGING64_PAGESZ;
  int n_frames = aligned_sz/PAGING64_PAGESZ;
// go through free_fp_list to get n sequent frames
  addr_t base_fpn =-1;
  struct framephy_struct *fp = krnl->mram->free_fp_list;
  struct framephy_struct *prev = NULL;


  //Relies on MEMPHY_put_freefp being orderly 
  while(fp != NULL){
    struct framephy_struct *cur = fp;
    int count = 1; 
    while(count<n_frames && cur->fp_next != NULL && cur->fp_next->fpn == cur->fpn+1){
      cur = cur->fp_next;
      count++;
    }
    if(count == n_frames){
      base_fpn = fp->fpn; //found
      if (prev == NULL)
        krnl->mram->free_fp_list = cur->fp_next;
      else
        prev->fp_next = cur->fp_next;
      cur->fp_next = NULL;

      //move to used_fp_list
      struct framephy_struct *node = fp;
    while (node != NULL) {
      struct framephy_struct *next = node->fp_next;
      node->fp_next = krnl->mram->used_fp_list;
      krnl->mram->used_fp_list = node;
      node = next;
  }
      break;
    }
    prev = fp;
    fp = fp->fp_next;
  }
  if(base_fpn==-1){
    return -1;
  }
  //virtual kernel address (calculate kernel and write into PTE)
  addr_t kernel_addr = base_fpn*PAGING64_PAGESZ;
  for(int i =0; i<n_frames; i++){
    addr_t pgn = (kernel_addr/PAGING64_PAGESZ)+i;
    addr_t fpn = base_fpn+i;
    pte_set_fpn(caller, pgn, fpn);
    krnl->krnl_pgd[pgn] = krnl->mm->pgd[pgn];
  }
  //update symrgtbl
  krnl->mm->symrgtbl[rgid].rg_start = kernel_addr;
  krnl->mm->symrgtbl[rgid].rg_end   = kernel_addr+ aligned_sz;
  *alloc_addr = kernel_addr;
  return 0;

}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* TODO: provide OS level management */
  
  //validate inputs 
  if (align<=0)
    return -1;
  if(align>size)
    return -1;
  //validate cache pool ID FIRST before accessing array
  if (cache_pool_id >=PAGING_MAX_SYMTBL_SZ)
    return -1;
  struct krnl_t *krnl = caller->krnl;
  int num_slots = (int)(size/align);
  if (num_slots<=0)
    return -1;
  if (krnl->mm->kcpooltbl[cache_pool_id].size != 0)  //check if created
    return -1;
  //allocate kernel memory for cache pool 
  addr_t pool_addr;
  if (__kmalloc(caller, -1, cache_pool_id, size, &pool_addr)!=0)
    return -1;
  
  //save meta datapool

  krnl->mm->kcpooltbl[cache_pool_id].size = size;
  krnl->mm->kcpooltbl[cache_pool_id].align = align;
  #ifdef MM64
  krnl->mm->kcpooltbl[cache_pool_id].storage = pool_addr;
  #else
  krnl->mm->kcpooltbl[cache_pool_id].storage = (uint32_t)pool_addr;
  #endif
  
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  if(cache_pool_id>=PAGING_MAX_SYMTBL_SZ){
    return -1;
  }
  if(reg_index >=PAGING_MAX_SYMTBL_SZ){
    return -1;
  }
  struct krnl_t *krnl = proc->krnl;
  
  //Check if pool exists
  if(krnl->mm->kcpooltbl[cache_pool_id].size == 0){
      return -1;
  }
  //Allocate from the cache pool
  addr_t slot_addr;
  int val = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &slot_addr);
  
  if (val != 0){
    return -1;
  }

  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  /* TODO: provide OS level management */
  struct krnl_t *krnl = caller->krnl;
  if(cache_pool_id<0 || cache_pool_id>=PAGING_MAX_SYMTBL_SZ){
    return -1;
  }
  //validate rgid
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ){
    return -1;
  }
  //kcpooltbl is struct kcache_pool_struct
  struct kcache_pool_struct *pool = &krnl->mm->kcpooltbl[cache_pool_id];
  //check valid pool
  if (pool->size <= 0 || pool->align <= 0) return -1;

  int num_slots = pool->size/pool->align;
  int slot_align = pool->align;
  
  #ifdef MM64
  addr_t pool_addr = pool->storage;
#else
  addr_t pool_addr = (addr_t)pool->storage;
#endif

  /* Find free slot - simple linear allocation */
  int free_slot_idx = -1;
  for (int i = 0; i < num_slots; i++) {
    addr_t slot_addr = pool_addr + (addr_t)(i * slot_align);
    int occupied = 0;
    for (int j = 0; j < PAGING_MAX_SYMTBL_SZ; j++) {
      if (j == rgid || j == cache_pool_id) continue;
      if (krnl->mm->symrgtbl[j].rg_start == slot_addr && krnl->mm->symrgtbl[j].rg_end   == slot_addr + slot_align)
      {
        occupied = 1;
        break;
      }
    }
    if (!occupied) {
      free_slot_idx = i;
      break;
    }
  }
  
  if (free_slot_idx == -1)
    return -1;  //no free slots
  
  //calculate slot address
  addr_t slot_addr = pool_addr + (addr_t)(free_slot_idx * slot_align);

  // Store allocation in symbol table
  krnl->mm->symrgtbl[rgid].rg_start = slot_addr;
  krnl->mm->symrgtbl[rgid].rg_end = slot_addr + slot_align;
  
  //update kernel page directory
  int pgn_start = slot_addr / PAGING64_PAGESZ;
  int pgn_end   = (slot_addr + slot_align - 1) / PAGING64_PAGESZ;


  //return allocated address
  *alloc_addr = slot_addr;
  return 0;

}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_user_mem(...)
  //__write_kernel_mem(...);
  BYTE data;
  for (uint32_t i = 0; i < size; i++){
    //read ith byte from user space
    int ret = __read_user_mem(caller, 0, source, offset + i, &data);
    if (ret !=0) return -1;
    //write to kernel space
    ret = __write_kernel_mem(caller, -1, destination, offset + i, data); //no & because we write
    if (ret != 0) return -1;
  }
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_kernel_mem(...)
  //__write_user_mem(...);
  BYTE data;
  for (uint32_t i = 0; i < size; i++){
    //read ith byte from kernel space
    int ret = __read_kernel_mem(caller, -1, source, offset + i, &data);
    if (ret !=0) return -1;
    //write to user
    ret = __write_user_mem(caller, 0, destination, offset + i, data);
    if (ret !=0) return -1;
  }
  return 0;
}


/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS memory operator for kernel memory region */
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  if(currg == NULL){ //kernel not vma
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start+offset >= currg->rg_end){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t v_addr = currg->rg_start + offset;
  int pgn = PAGING_PGN(v_addr);
  int off = PAGING_OFFST(v_addr);

  //Can't get pte from user space using pte_get_entry. Learned the hard way
  uint32_t pte = caller->krnl->krnl_pgd[pgn];  

  if (!PAGING_PAGE_PRESENT(pte)) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1; 
  }

  int fpn = PAGING_FPN(pte);
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  MEMPHY_read(caller->krnl->mram, phyaddr, data);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS memory operator for kernel memory region */
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  if(currg == NULL){ //kernel not vma
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start+offset >= currg->rg_end){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t v_addr = currg->rg_start + offset;
  int pgn = PAGING_PGN(v_addr);
  int off = PAGING_OFFST(v_addr);

  //Can't get pte from user space using pte_get_entry. Learned the hard way
  uint32_t pte = caller->krnl->krnl_pgd[pgn];  

  if (!PAGING_PAGE_PRESENT(pte)) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1; 
  }

  int fpn = PAGING_FPN(pte);
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  MEMPHY_write(caller->krnl->mram, phyaddr, value);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ...
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if(currg == NULL || cur_vma ==NULL){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start >= currg->rg_end ){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start+offset >= currg->rg_end){
    pthread_mutex_unlock(&mmvm_lock);   
    return -1; //offset outside region bound
  }
  //int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
  pthread_mutex_unlock(&mmvm_lock);

  return pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);
}

/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ...
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if(currg == NULL || cur_vma ==NULL){
    pthread_mutex_unlock(&mmvm_lock); 
    return -1;
  }
  if(currg->rg_start >= currg->rg_end ){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(currg->rg_start+offset >= currg->rg_end){
    pthread_mutex_unlock(&mmvm_lock);     
    return -1; //offset outside region bound
  }
  //int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
  pthread_mutex_unlock(&mmvm_lock);
  return pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = pte_get_entry(caller, pagenum);

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else if (PAGING_PAGE_SWAPPED(pte)) {
    fpn = PAGING_SWP(pte);
    MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
    //else: unmapped pages, skip
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  //FIFO
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  if (prev == NULL) mm->fifo_pgn = NULL; //Handle the 1 node case
  else prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL) 
    return -1;

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
