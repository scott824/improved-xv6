#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return proc->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  // LWP2 - 1.4.1.1 return topofheap.
  if(proc->threadof == 0)
    addr = proc->topofheap;
  else
    addr = proc->threadof->topofheap;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(proc->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// wrapper function for yield system call
int
sys_yield(void)
{
  yield();
  return 0;
}

// function for getlev system call
int
sys_getlev(void)
{
  return proc->level;
}

// wrapper function for set_cpu_share system call
int
sys_set_cpu_share(void)
{
  int percent;
  if(argint(0, &percent) < 0)
    return -1;
  return set_cpu_share(percent);
}

// wrapper function for thread_create system call
int
sys_thread_create(void)
{
  thread_t *thread;
  void *(*start_routine)(void*);
  uint arg;
  if(argint(0, (int*)&thread) < 0)
    return -1;
  if(argint(1, (int*)&start_routine) < 0)
    return -1;
  if(argint(2, (int*)&arg) < 0)
    return -1;
  return thread_create(thread, start_routine, (void*)arg);
}

// wrapper function for thread_exit system call
int
sys_thread_exit(void)
{
  void *retval;
  if(argint(0, (int*)&retval) < 0)
    return -1;
  thread_exit(retval);
  return 0;
}

// wrapper function for thread_join system call
int
sys_thread_join(void)
{
  thread_t thread;
  void **retval;
  if(argint(0, (int*)&thread) < 0)
    return -1;
  if(argint(1, (int*)&retval) < 0)
    return -1;
  return thread_join(thread, retval);
}

