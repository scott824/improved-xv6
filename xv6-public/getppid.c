#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

// get parants pid
int
getppid()
{
    return proc->parent->pid;
}

// wrapper
int
sys_getppid()
{
    return getppid();
}
