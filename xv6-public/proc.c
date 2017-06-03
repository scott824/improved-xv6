#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// ticks of quantum each level will use
const int quantum[NUMLEVEL] = {5, 10, 20};

// 1.1 Process table which will save all the processes
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// 1.4 Process pointer table for MLFQ
struct pptrtable {
  struct spinlock lock;
  struct proc *proc[NPROC];
};

// 1.3 Per-stride process state
struct strideproc {
  struct spinlock lock;
  struct pptrtable pptable; // save pointer of procs in ptable
  uint tickets;
  uint stride;
  uint pass;
  uint usedticks;    // save usedticks for boosting
  uint sid;          // stride proc id
  uint currentproc;  // index of running proc in pptable
};

// 1.2 Process table for stride process
struct {
    struct spinlock lock;
    struct strideproc strideproc[NPROC];
} stridetable;

static struct proc *initproc;

// MLFQ stride proc(initial stride)
static struct strideproc *MLFQ;
// 2.2 current stride proc
static struct strideproc *current;

int nextpid = 1;
int nextsid = 1;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&stridetable.lock, "stridetable");

  acquire(&stridetable.lock);

  struct strideproc *p;
  for(p = stridetable.strideproc; p < &stridetable.strideproc[NPROC]; p++){
    initlock(&p->pptable.lock, "pptable");
    initlock(&p->lock, "strideproc");
  }

  // init first stride proc (MLFQ)
  stridetable.strideproc->tickets = ENTIRETICKETS;
  stridetable.strideproc->stride = ENTIRETICKETS*ACCURATENUM / stridetable.strideproc->tickets;
  stridetable.strideproc->pass = 0;
  stridetable.strideproc->usedticks = 0;
  stridetable.strideproc->sid = nextsid++;

  // first stride proc is MLFQ
  MLFQ = stridetable.strideproc;
  current = MLFQ;

  release(&stridetable.lock);
#if LOG == TRUE
  cprintf("LOG: MLFQ's tickets = %d\n", MLFQ->tickets);
#endif
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  acquire(&current->pptable.lock);
  // save proc pointer in stride proc pptable
  int i;
  for(i = 0; i < NPROC; i++)
    if(!current->pptable.proc[i]){
      current->pptable.proc[i] = p;
      break;
    }
  release(&current->pptable.lock);
  
  // init proc properties for MLFQ
  p->level = 0;
  p->usedticks = 0;

  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  // milestone 2
  p->topofheap = PGSIZE;
  p->baseofstack = KERNBASE - PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;

  if(proc->threadof != 0){
    sz = proc->threadof->topofheap;
  }else{
    sz = proc->topofheap;
  }
  //cprintf("LOG: %d %s sbrk start growproc topofheap = %x\n", proc->pid, proc->name, sz);

  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }

  if(proc->threadof != 0){
    proc->threadof->topofheap = sz;
  }
  if(n > 0 || proc->threadof == 0){
    proc->topofheap = sz;
  }
  //cprintf("LOG: %d %s sbrk end growproc topofheap = %x\n", proc->pid, proc->name, sz);

  switchuvm(proc);
  return 0;
}

int
growstack(int isgrow)
{
  uint baseofstack;
  uint topofheap;
  uint new_baseofstack;

  if(proc->threadof != 0){
    baseofstack = proc->threadof->baseofstack;
    topofheap = proc->threadof->topofheap;
  }else{
    baseofstack = proc->baseofstack;
    topofheap = proc->topofheap;
  }

  if(isgrow){
    if(baseofstack - 2*PGSIZE < topofheap){
      cprintf("LOG: growstack - stack can't be allocate more\n");
      return -1;
    }

    //cprintf("LOG: %d %s start allocustack: baseofstack = %x\n", proc->pid, proc->name, baseofstack);

    new_baseofstack = baseofstack - 2*PGSIZE;
    if((allocuvm(proc->pgdir, baseofstack - 2*PGSIZE, baseofstack)) == 0)
      return -1;
    clearpteu(proc->pgdir, (char*)new_baseofstack);
  }else{
    new_baseofstack = baseofstack + 2*PGSIZE;
    if((deallocuvm(proc->pgdir, new_baseofstack, baseofstack)) == 0)
      return -1;
  }

  if(proc->threadof != 0){
    proc->threadof->baseofstack = new_baseofstack;
  }else{
    proc->baseofstack = new_baseofstack;
  }
  //cprintf("LOG: %d %s baseofstack = %x\n", proc->pid, proc->name, new_baseofstack);

  switchuvm(proc);
  return baseofstack;
}


// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  //cprintf("LOG: %d %s start fork\n", proc->pid, proc->name);
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from p.
  // TODO: should also copy stack area
  if((np->pgdir = copyuvm(proc->pgdir, proc->topofheap, proc->baseofstack)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->topofheap = proc->topofheap;
  np->baseofstack = proc->baseofstack;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  //cprintf("LOG: %d %s end fork\n", proc->pid, proc->name);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  //cprintf("LOG: %d %s enter exit\n", proc->pid, proc->name);
  if(proc == initproc)
    panic("init exiting");
  cleanup_all(proc->pgdir);
  // Jump into the scheduler, never to return.
  //cprintf("LOG: exit before enter sched(), sid: %d, pass: %d\n", current->sid, current->pass);
  sched();
  panic("zombie exit");
}

void
cleanup_all(pde_t *pgdir)
{
  /*
  acquire(&ptable.lock);
  cprintf("LOG: %d %s cleanup_all\n", proc->pid, proc->name);
  int i;
  cprintf("LOG: Process LIST - [");
  for(i = 0; i < 30; i++){
    cprintf("%d:%s-s%d-p%d-t%d, ", ptable.proc[i].pid, ptable.proc[i].name, ptable.proc[i].state, ptable.proc[i].parent->pid, ptable.proc[i].threadof->pid);
  }
  cprintf("]\n");
  release(&ptable.lock);*/

  struct proc *p;

  //cleanup_fs(proc);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pgdir == pgdir)
      cleanup_fs(p);

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  if(pgdir == proc->pgdir)
    wakeup1(proc->parent);

  //cleanup_child(proc);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pgdir == pgdir)
      cleanup_child(p);

#if LOG
  cprintf("LOG: Exit from %d %s\n", proc->pid, proc->name);
  cprintf("LOG: Process LIST - [");
  for(i = 0; i < 30; i++){
    cprintf("%d:%s-s%d-p%d-t%d, ", ptable.proc[i].pid, ptable.proc[i].name, ptable.proc[i].state, ptable.proc[i].parent->pid, ptable.proc[i].threadof->pid);
  }
  cprintf("]\n");
#endif

  if(pgdir != proc->pgdir){
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->pgdir == pgdir)
        p->pgdir = proc->pgdir;
    release(&ptable.lock);
  }
}

// cleanup process's(thread's) child, cleanup thread
void
cleanup_child(struct proc *p)
{
  struct proc *i;
  // Pass abandoned children to init.
  for(i = ptable.proc; i < &ptable.proc[NPROC]; i++){
    if(i->parent == p){
      i->parent = initproc;
      if(i->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  p->state = ZOMBIE;
}

// cleanup file system related works
void
cleanup_fs(struct proc *p)
{
  int fd;

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p, *i;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    //cprintf("LOG: %d start wait by wake up\n", proc->pid);
    // clear process
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc || p->threadof != 0)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        //cprintf("clear %d\n", p->pid);
        // Found one.
        // clear threads
        for(i = ptable.proc; i < &ptable.proc[NPROC]; i++){
          if(i->pgdir == p->pgdir && i->threadof != 0){
            if(i->state != ZOMBIE)
              panic("threads should be ZOMBIE if process exit");
            //cprintf("clear %d, threadof: %d\n", i->pid, i->threadof->pid);
            freeThreadPCB(i);
            removeProcPtr(i);
          }
        }
        pid = p->pid;
        freevm(p->pgdir);
        freeThreadPCB(p);

        release(&ptable.lock);

        
        /*
        int j;
        cprintf("LOG: Process LIST - [");
        for(j = 0; j < 30; j++){
          cprintf("%d:%s-s%d-p%d-t%d, ", ptable.proc[j].pid, ptable.proc[j].name, ptable.proc[j].state, ptable.proc[j].parent->pid, ptable.proc[j].threadof->pid);
        }
        cprintf("]\n");*/

        // remove all the pointer of this proc
        removeProcPtr(p);

        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

// 2. Stride scheduler
void
scheduler(void)
{
    int i, minpass, minpindex;

    for(;;){
      sti();
      acquire(&ptable.lock);

      // 2.1.1 find stride proc which has the smallest pass
      minpass = MAXINT;
      minpindex = 0;
      for(i = 0; i < NPROC; i++){
        if(stridetable.strideproc[i].sid)
        if(minpass > stridetable.strideproc[i].pass){
          minpass = stridetable.strideproc[i].pass;
          minpindex = i;
        }
      }

#if LOG == TRUE
      //cprintf("LOG: found min pass stride index %d\n", minpindex);
#endif

      // 2.1.2 change current stride proc;
      current = &stridetable.strideproc[minpindex];

      // check is it empty stride proc
      for(i = 0; i < NPROC; i++)
        if(current->pptable.proc[i] != 0)
          break;

      if(i == NPROC){
        // 2.1.3 remove stride proc
        MLFQ->tickets += current->tickets;
        MLFQ->stride = ENTIRETICKETS*ACCURATENUM / MLFQ->tickets;
        current->tickets = 0;
        current->stride = 0;
        current->pass = 0;
        current->usedticks = 0;
        current->sid = 0;

#if LOG == TRUE
        //cprintf("LOG: Remove empty stride proc %d\n", current->sid);
#endif

      }else{
        // 2.1.4 start MLFQ scheduler of current stride
        MLFQ_scheduler();
      }

      release(&ptable.lock);
    }
}

// 3. MLFQ scheduler
void
MLFQ_scheduler(void)
{
  int i;
  int currentqueue = 0, currentproc = current->currentproc;
  int firstloop = TRUE;
  struct proc **ppArr = current->pptable.proc;
  struct proc *p = ppArr[currentproc];

  // 3.1.1 if proc doesn't use his quantum yet -> run again
  if(p && p->state == RUNNABLE && p->usedticks < quantum[p->level]){
    goto found;
  }

  // 3.1.2 if proc use all of his quantum
  if(p && p->usedticks >= quantum[p->level]){

#if LOG == TRUE
    //cprintf("LOG: %d %s proc use all its quantum, level: %d\n", p->pid, p->name, p->level);
#endif

    if(p->level < 2)
      p->level++;
    p->usedticks = 0;
  }

  // 3.1.3 find another proc to run
  while(currentqueue < NUMLEVEL){
    for(i = currentproc + 1; i < NPROC; i++){
      if(!ppArr[i] || ppArr[i]->state != RUNNABLE || ppArr[i]->level != currentqueue)
        continue;
      else{
        current->currentproc = i;
        p = ppArr[i];
        goto found;
      }
    }
    if(firstloop){
      firstloop = FALSE;
      currentproc = -1;
    } else
      currentqueue++;
  }

#if LOG == TRUE
  //cprintf("LOG: NO process to run in %d stride proc\n", current->sid);
#endif

  // 3.1.4 return if there is no proc to run
  current->pass++;
  return;

found:
  // 3.1.5 swtch

#if LOG == TRUE
  //if(p->pid == 10000)
    cprintf("LOG: swtch to %d %s\n", p->pid, p->name);
#endif

  proc = p;
  switchuvm(p);
  p->state = RUNNING;
  swtch(&cpu->scheduler, p->context);
  switchkvm();
  proc = 0;
}

// 3.2 boosting
void
boost(void)
{

#if LOG == TRUE
  cprintf("LOG: Boost!!!\n");
#endif

  int i;
  for(i = 0; i < NPROC; i++){
    if(current->pptable.proc[i]){
      current->pptable.proc[i]->level = 0;
      current->pptable.proc[i]->usedticks = 0;
    }
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");

  // 3.2.1-2 increase ticks process used
  proc->usedticks++;
  current->usedticks++;
  // 3.2.3 increase pass
  current->pass += current->stride;

  // 3.2.4 Boost if current MLFQ use 100 ticks
  if(current->usedticks >= 100){
    boost();
    current->usedticks = 0;
  }

  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
#if LOG == TRUE
  //cprintf("YIELD: %d %s, use %d ticks.\n", proc->pid, proc->name, proc->usedticks);
#endif
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// 2.3 Move process to Stride scheduler
int
set_cpu_share(int percent)
{

#if LOG == TRUE
  cprintf("LOG: %d %s set_cpu_share %d%\n", proc->pid, proc->name, percent);
  cprintf("LOG: MLFQ->tickets = %d, percentage = %d\n", MLFQ->tickets, MLFQ->tickets * 100 / ENTIRETICKETS);
#endif

  // 2.3.1 check MLFQ will less than 20%
  if(MLFQ->tickets*100 / ENTIRETICKETS - percent < 20){
    cprintf("ERROR: MLFQ should get more than 20%% of CPU\n");
    return 1;
  }

  // 2.3.2 check is there room for new stride proc
  struct strideproc *p;
  for(p = stridetable.strideproc; p < &stridetable.strideproc[NPROC]; p++)
    if(p->sid == 0)
      goto found;
  cprintf("ERROR: There is no more room for new stride proc\n");
  return 1;

found:
  acquire(&ptable.lock);

  // 2.3.3 init new stride proc
  p->tickets = ENTIRETICKETS * percent / 100;
  p->stride = ENTIRETICKETS*ACCURATENUM / p->tickets;
  p->pass = getminpass() - p->stride;
  p->usedticks = 0;
  p->sid = nextsid++;
  struct proc *i;
  int count = 0;
  for(i = ptable.proc; i < &ptable.proc[NPROC]; i++){
    if(i->pgdir == proc->pgdir){
      removeProcPtr(i);
      p->pptable.proc[count++] = i;
    }
  }
  /*struct strideproc *sp;
  int j, find = 0;

  acquire(&stridetable.lock);
  cprintf("LOG: %d %s set_cpu_share %d,sid: %d stride: %d pass: %d\n", proc->pid, proc->name, percent, p->sid, p->stride, p->pass);
  for(sp = stridetable.strideproc; sp < &stridetable.strideproc[NPROC]; sp++){
    for(j = 0; j < NPROC; j++)
      if(sp->pptable.proc[j] != 0){
        cprintf("sid: %d, pid: %d %s, threadof: %d, state: %d\n", sp->sid, sp->pptable.proc[j]->pid, sp->pptable.proc[j]->name, sp->pptable.proc[j]->threadof->pid, sp->pptable.proc[j]->state);
      }
  }
  release(&stridetable.lock);*/

  //p->pptable.proc[0] = proc;
  //current->pptable.proc[current->currentproc] = 0;

  // 2.3.4 change MLFQ's tickets and stride
  MLFQ->tickets -= p->tickets;
  MLFQ->stride = ENTIRETICKETS*ACCURATENUM / MLFQ->tickets;

  release(&ptable.lock);

  return 0;
}

// get the smallest pass from stridetable
int
getminpass(void)
{
  int i, minpass = MAXINT;
  for(i = 0; i < NPROC; i++){
    if(stridetable.strideproc[i].sid)
    if(minpass > stridetable.strideproc[i].pass){
      minpass = stridetable.strideproc[i].pass;
    }
  }
  return minpass;
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;

  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// remove proc pointer from pptable
int
removeProcPtr(struct proc *p)
{
  struct strideproc *sp;
  int i, find = 0;

  acquire(&stridetable.lock);
  for(sp = stridetable.strideproc; sp < &stridetable.strideproc[NPROC]; sp++)
    for(i = 0; i < NPROC; i++)
      if(sp->pptable.proc[i] == p){
        sp->pptable.proc[i] = 0;
        find = 1;
      }
  release(&stridetable.lock);
  
#if LOG == TRUE
  cprintf("LOG: success remove process pointer!!\n");
#endif

  return find;
}

// thread syscalls

// LWP 1 thread_create
int
thread_create(thread_t *thread, void *(*start_routine)(void*), void *arg)
{
  struct proc *np;

  // LWP 1.4.1 allocate thread's PCB
  if((np = allocproc()) == 0){
    cprintf("LOG: Can't alloc more PCB\n");
    return -1;
  }

  // LWP 1.4.2 return thread's pid
  *thread = np->pid;

  // LWP 1.4.3
  // thread's PCB will remember it's main process
  // also remember where thread_join() called for this thread
  if(proc->threadof == 0){
    np->threadof = proc;
  }else{
    // if thread_create is called in thread
    np->threadof = proc->threadof;
  }
  np->returnto = proc;

  // LWP 1.4.4
  // allocate new user stack for thread
  /*
  if(growproc(PGSIZE) == -1){
    cprintf("LOG: Can't grow proc\n");
    return -1;
  }
  */

  // LWP 1.4.5
  // fill the new user stack
  uint sp, ustack[2];
  //cprintf("LOG: %d %s thread_create growstack start\n", proc->pid, proc->name);
  sp = growstack(TRUE);
  //cprintf("LOG: %d %s thread_create growstack %x to %x\n", proc->pid, proc->name, sp, proc->baseofstack);
  if(sp == -1){
    cprintf("LOG: can't grow user stack\n");
    return -1;
  }

  ustack[0] = 0xffffffff; // thread will never return in normal
  ustack[1] = (uint)arg;
  
  sp -= 2*4;
  if(copyout(proc->pgdir, sp, ustack, 2*4) < 0){
    cprintf("LOG: Can't copy argument to user stack\n");
    return -1;
  }

  // LWP 1.4.6 Initial new thread's PCB
  // share Address Space
  np->pgdir = proc->pgdir;

  // each thread's sz is address of top of it's user stack
  np->sz = proc->sz;
  np->topofheap = proc->topofheap;
  np->baseofstack = proc->baseofstack;

  // set PCB infos same as main process
  np->parent = proc->parent;
  int i;
  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
  safestrcpy(np->name, proc->name, sizeof(proc->name));

  *np->tf = *proc->tf;

  // LWP 1.4.7
  // LWP 1.2.2 - 1,2 eip, esp for usermode
  // set Program Counter to `start_routine` function
  np->tf->eip = (uint)start_routine;
  // set Stack Pointer to new user stack
  np->tf->esp = sp;

  acquire(&ptable.lock);
  // LWP 1.4.8
  np->state = RUNNABLE;
  release(&ptable.lock);

  //cprintf("LOG: %d %s create thread %d %s\n\n", proc->pid, proc->name, np->pid, np->name);
  return 0;
}

// LWP 2 thread_exit
void
thread_exit(void *retval)
{
  struct proc *p;

  //cprintf("LOG: %d %s thread_exit\n", proc->pid, proc->name);

  // LWP 2.1.1
  // if call by main process
  if(proc->threadof == 0){
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->threadof == proc){
        thread_join(p->pid, 0);
      }
    exit();
  }
  
  // LWP 2.1.2

  // LWP 2.1.2.1
  // cleanup file I/O
  cleanup_fs(proc);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->parent == proc)
      cleanup_fs(p);

  acquire(&ptable.lock);

  // LWP 2.1.2.2
  // wake up thread or main process which call thread_join for this thread
  wakeup1(proc->returnto);

  // LWP 2.1.2.3 set return value
  proc->threadret = (uint)retval;

  // LWP 2.1.2.1 change thread's child's parent to `initproc`
  cleanup_child(proc);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->parent == proc)
      cleanup_child(p);

  // LWP 2.1.2.4 thread will never return
  sched();
  panic("zombie thread exit");
}

// LWP 3 thread_join
int
thread_join(thread_t thread, void **retval)
{
  struct proc *p;

  acquire(&ptable.lock);

  // LWP 3.2.1 find thread
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == thread){
    found:
      // LWP 3.2.2 check ZOMBIE
      if(p->state == ZOMBIE){
        // LWP 3.2.5 return value
        if(retval != 0)
          *retval = (void*)p->threadret;

        freeThreadPCB(p);

        release(&ptable.lock);

        removeProcPtr(p);
        // LWP 3.2.6 free user stack
        if(cleanup_ustack() != 0)
          return -1;

        return 0;
      }
      else{
        // LWP 3.2.3 wait for thread to end
        //cprintf("LOG: %d %s thread_join: wait for %d %s\n", proc->pid, proc->name, p->pid, p->name);
        sleep(proc, &ptable.lock);
        // LWP 3.2.4 thread call wakeup
        goto found;
      }
    }
  }

  // there is no thread
  release(&ptable.lock);
  return -1;
}

void
freeThreadPCB(struct proc *p)
{
  // LWP 3.2.6 free thread kstack
  kfree(p->kstack);
  // LWP 3.2.7 initialize thread's PCB
  p->kstack = 0;
  p->pgdir = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->sz = 0;
  p->topofheap = 0;
  p->baseofstack = 0;
  p->threadof = 0;
  p->returnto = 0;
  p->threadret = 0;
  p->usedticks = 0;
  p->level = 0;
  p->state = UNUSED;
}

// LWP 3.1.3
// dealloc user stack from Address Space
int
cleanup_ustack()
{
  //cprintf("LOG: %d %s cleanup_ustack()\n", proc->pid, proc->name);
  struct proc *p;
  struct proc *mainPCB;
  uint baseofstack;

  if(proc->threadof != 0)
    mainPCB = proc->threadof;
  else
    mainPCB = proc;

  // top Address of allocated stacks
  baseofstack = mainPCB->baseofstack;

  for(;;){
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->threadof == mainPCB){
        if(p->baseofstack == baseofstack)
          goto exist; // there is thread which has `sz` top stack address
      }
    }
    // there is no thread which has `sz` top stack address

    // dealloc user stack
    //cprintf("LOG: %d %s dealloc ustack of %x\n", proc->pid, proc->name, baseofstack);
    if(growstack(FALSE) == -1){
      cprintf("LOG: Can't dealloc stack\n");
      return -1;
    }
    baseofstack += 2*PGSIZE;

    // should not dealloc mainPCB's user stack
    if(mainPCB->tf->esp < (baseofstack + 2*PGSIZE))
      break;
  }

exist:
  return 0;
}
