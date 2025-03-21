#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64 count_freeproc(void);
uint64 count_freemem(void);

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
#ifdef LAB_TRAPS

  backtrace();

#endif
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void){
  int mask;

  argint(0, &mask);
  myproc()->tra_mask = mask;

  return 0;
}

uint64
sys_sysinfo(void){
  struct proc* p = myproc();

  uint64 info_ptr;
  argaddr(0, &info_ptr);

  struct sysinfo info;

  info.freemem = count_freemem();
  info.nproc = count_freeproc();

  if(copyout(p->pagetable, info_ptr, (char*)&info, sizeof(info)) == -1){
      return -1;
  }
  return 0;
}

uint64
sys_sigalarm(void){
    struct proc* p = myproc();
    int interval;
    uint64 handler;

    argint(0, &interval);
    argaddr(1, &handler);

    p->interval = interval;
    p->handler = (void (*)(void))handler;
    return 0;
}

uint64
sys_sigreturn(void){
    struct proc* p = myproc();
    if(p->is_handler){
      *(p->trapframe) = *(p->prev_trapf);

      p->trapframe->a0 = p->prev_a0;
      p->is_handler = 0;
    }
    return 0;
}