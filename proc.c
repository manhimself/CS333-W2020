#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#ifdef CS333_P2
#include "uproc.h"
#endif

#ifdef CS333_P3
#define statecount NELEM(states)
#endif

static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};

#ifdef CS333_P3
// record with head and tail pointer for constant-time access to the beginning
// and end of a linked list of struct procs.  use with stateListAdd() and
// stateListRemove().
struct ptrs {
  struct proc* head;
  struct proc* tail;
};
#endif

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  #ifdef CS333_P3
  struct ptrs list[statecount];
  #endif

  #ifdef CS333_P4
  struct ptrs ready[MAXPRIO + 1];
  uint PromoteAtTime;
  #endif

} ptable;

// list management function prototypes
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void assertState(struct proc*, enum procstate, const char *, int);
#endif

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
#ifdef CS333_P3
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  int found = 0;
  for(p=ptable.list[UNUSED].head; p; p=p->next)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }

  #ifdef CS333_P3
  if(stateListRemove(&ptable.list[UNUSED], p) == -1){
    panic("no unused");
  }
  assertState(p, UNUSED, __FUNCTION__, __LINE__);
  #endif
  p->state = EMBRYO;
  p->pid = nextpid++;
  
  #ifdef CS333_P3
  stateListAdd(&ptable.list[EMBRYO],p);
  #endif


  #ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  #endif 
  
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    #ifdef CS333_P3
    acquire(&ptable.lock);
    if(stateListRemove(&ptable.list[EMBRYO], p) == -1){
      panic("no unused");
    }
    assertState(p, EMBRYO, __FUNCTION__, __LINE__);
    #endif 
    p->state = UNUSED;

    #ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], p);
    #endif

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
#else
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  int found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }
  p->state = EMBRYO;
  p->pid = nextpid++;

  #ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  #endif 
  
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
#endif

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  #ifdef CS333_P3
  //init process list
  initProcessLists();
  initFreeList();
  #endif

  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
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
  #ifdef CS333_P3
	if(stateListRemove(&ptable.list[EMBRYO],p)== -1){
    cprintf("embryo");
  };
	assertState(p,EMBRYO, __FUNCTION__, __LINE__);
	#endif

  p->state = RUNNABLE;

  #ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE],p);
  #endif
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}
// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
#ifdef CS333_P3
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    #ifdef CS333_P3
    acquire(&ptable.lock);
    if(stateListRemove(&ptable.list[EMBRYO], np) == -1){
      panic("no np->state");
    }
    assertState(np, EMBRYO, __FUNCTION__, __LINE__);
    #endif
    np->state = UNUSED;

    #ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], np);
    release(&ptable.lock);
    #endif
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  #ifdef CS333_P3
  if(stateListRemove(&ptable.list[EMBRYO], np) == -1){
    panic("no np->state");
  }
  assertState(np, EMBRYO, __FUNCTION__, __LINE__);
  #endif
  np->state = RUNNABLE;

  #ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], np);
  #endif

  #ifdef CS333_P2
	//copy uid and gid for the child process.
	np->uid = curproc->uid;
	np->gid = curproc->gid;
	np->parent = curproc;
	#endif
  release(&ptable.lock);

  return pid;
}
#else
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  np->state = RUNNABLE;
  #ifdef CS333_P2
	//copy uid and gid for the child process.
	np->uid = curproc->uid;
	np->gid = curproc->gid;
	np->parent = curproc;
	#endif
  release(&ptable.lock);

  return pid;
}
#endif

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifdef CS333_P3
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.


  // we know the order of the procstate enum. 
  //0       1         2         3         4         5
  //UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE
 

  for(int i = 1; i <= 5; i++) {
    for(p=ptable.list[i].head; p; p=p->next){
      if(p->parent == curproc){
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
      }
    }
  }



  // Jump into the scheduler, never to return.
  #ifdef CS333_P3
	if(stateListRemove(&ptable.list[RUNNING],curproc) == -1)
    panic("no item in RUNNING");
	assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  #endif

  curproc->state = ZOMBIE;

  #ifdef CS333_P3
  stateListAdd(&ptable.list[ZOMBIE], curproc);
  #endif

#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#else
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#endif


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P3
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(int i = 1; i <= 5; i++){
      for(p=ptable.list[i].head; p; p=p->next){
        if(p->parent != curproc)
          continue;
        havekids = 1;
        if(p->state == ZOMBIE){
          // Found one.
          pid = p->pid;
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          
          #ifdef CS333_P3
          if(stateListRemove(&ptable.list[ZOMBIE],p) == -1)
            panic("no item in ZOMBIE list");
          assertState(p, ZOMBIE, __FUNCTION__, __LINE__);
          #endif 
          p->state = UNUSED;
          #ifdef CS333_P3
          stateListAdd(&ptable.list[UNUSED], p);
          #endif

          release(&ptable.lock);
          return pid;
        }
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

#else
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#endif


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifdef CS333_P3
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p=ptable.list[RUNNABLE].head; p; p=p->next){
      if(p->state != RUNNABLE)
        panic("wrong item in RUNNABLE list");
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;


      switchuvm(p);
      #ifdef CS333_P3
      if(stateListRemove(&ptable.list[RUNNABLE],p) == -1)
        panic("no item in RUNNING list");
      assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
      #endif 
      p->state = RUNNING;

      #ifdef CS333_P3
      stateListAdd(&ptable.list[RUNNING], p);
      #endif

      #ifdef CS333_P2
      p->cpu_ticks_in = ticks;
      #endif
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#else
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;


      switchuvm(p);
      p->state = RUNNING;
      #ifdef CS333_P2
      p->cpu_ticks_in = ticks;
      #endif
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif

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
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  #ifdef CS333_P2
  p->cpu_ticks_total += ticks - p->cpu_ticks_in;
  #endif
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
#ifdef CS333_P3
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  #ifdef CS333_P3
  if(stateListRemove(&ptable.list[RUNNING],curproc) == -1)
    panic("no item in RUNNING list");
	assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  #endif
  curproc->state = RUNNABLE;
  #ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], curproc);
  #endif

  sched();
  release(&ptable.lock);
}
#else
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
#endif
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

#ifdef CS333_P3
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  #ifdef CS333_P3
  if(stateListRemove(&ptable.list[RUNNING],p) == -1)
    panic("no item in RUNNING list");
	assertState(p, RUNNING, __FUNCTION__, __LINE__);
  #endif
  p->chan = chan;
  p->state = SLEEPING;
  #ifdef CS333_P3
  stateListAdd(&ptable.list[SLEEPING], p);
  #endif

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#else
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#endif

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifdef CS333_P3
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p=ptable.list[SLEEPING].head; p; p=p->next){
    if(p->state == SLEEPING && p->chan == chan){
      #ifdef CS333_P3
      if(stateListRemove(&ptable.list[SLEEPING], p) == -1)
        panic("no item in sleeping list");
      assertState(p, SLEEPING, __FUNCTION__, __LINE__);
      #endif
      p->state = RUNNABLE;
      #ifdef CS333_P3
      stateListAdd(&ptable.list[RUNNABLE], p);
      #endif
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}
#else
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
#endif

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
#ifdef CS333_P3
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(int i=1; i<=5; i++){
    for(p=ptable.list[i].head; p; p=p->next){
      if(p->pid == pid){
        p->killed = 1;
        // Wake process from sleep if necessary.
        if(p->state == SLEEPING){
          #ifdef CS333_P3
          if(stateListRemove(&ptable.list[SLEEPING], p) == -1){
            panic("no item in sleeping list");
          }
          assertState(p, SLEEPING, __FUNCTION__, __LINE__);
          #endif
          p->state = RUNNABLE;

          #ifdef CS333_P3
          stateListAdd(&ptable.list[RUNNABLE], p);
          #endif

        }
        release(&ptable.lock);
        return 0;
      }
    }
  }
  release(&ptable.lock);
  return -1;
}
#else
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
#endif
//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

#if defined(CS333_P4)
void
procdumpP4(struct proc *p, char *state_string)
{
  cprintf("TODO for Project 4, delete this line and implement procdumpP4() in proc.c to print a row\n");
  return;
}
#elif defined(CS333_P3)
void
procdumpP3(struct proc *p, char *state_string)
{
  cprintf("%d\t", p->pid);
  char name_str[5];
  name_str[5] = '\0';
  strncpy(name_str, p->name, 5);

  cprintf("%s\t\t", name_str);	
	cprintf("%d\t",p->uid);
	cprintf("%d\t",p->gid);
	if(p->parent != NULL)
		cprintf("%d\t",p->parent->pid);
	else
		cprintf("%d\t",p->pid);
	
	int T1 = (ticks-(p->start_ticks)) % 10;
	int T2 = ((ticks-(p->start_ticks)) % 100 )/10;
	int T3 = ((ticks-(p->start_ticks)) % 1000)/100;
	int T4 = (ticks-(p->start_ticks)) / 1000;
	cprintf("%d%s%d%d%d\t", T4,".",T3,T2,T1);
	int cputicksDiff = p->cpu_ticks_total;
	int T11 = cputicksDiff % 10;
	int T22 = (cputicksDiff % 100) / 10;
	int T33 = (cputicksDiff % 1000) / 100;
	int T44 = (cputicksDiff / 1000);
	cprintf("%d%s%d%d%d\t", T44,".",T33,T22,T11);
	cprintf("%s\t", state_string);
	cprintf("%d\t", p->sz);
  return;

}
#elif defined(CS333_P2)
void
procdumpP2(struct proc *p, char *state_string)
{
	cprintf("%d\t", p->pid);
  char name_str[5];
  name_str[5] = '\0';
  strncpy(name_str, p->name, 5);

  cprintf("%s\t\t", name_str);	
	cprintf("%d\t",p->uid);
	cprintf("%d\t",p->gid);
	if(p->parent != NULL)
		cprintf("%d\t",p->parent->pid);
	else
		cprintf("%d\t",p->pid);
	
	int T1 = (ticks-(p->start_ticks)) % 10;
	int T2 = ((ticks-(p->start_ticks)) % 100 )/10;
	int T3 = ((ticks-(p->start_ticks)) % 1000)/100;
	int T4 = (ticks-(p->start_ticks)) / 1000;
	cprintf("%d%s%d%d%d\t", T4,".",T3,T2,T1);
	int cputicksDiff = p->cpu_ticks_total;
	int T11 = cputicksDiff % 10;
	int T22 = (cputicksDiff % 100) / 10;
	int T33 = (cputicksDiff % 1000) / 100;
	int T44 = (cputicksDiff / 1000);
	cprintf("%d%s%d%d%d\t", T44,".",T33,T22,T11);
	cprintf("%s\t", state_string);
	cprintf("%d\t", p->sz);
  return;
}
#elif defined(CS333_P1)
void
procdumpP1(struct proc *p, char *state_string)
{
	cprintf("%d\t", p->pid);
  cprintf("%s\t\t", p->name);	
	int T1 = (ticks-(p->start_ticks)) % 10;
	int T2 = ((ticks-(p->start_ticks)) % 100 )/10;
	int T3 = ((ticks-(p->start_ticks)) % 1000)/100;
	int T4 = (ticks-(p->start_ticks)) / 1000;
	cprintf("%d%s%d%d%d\t", T4,".",T3,T2,T1);
	cprintf("%s\t", state_string);
	cprintf("%d\t", p->sz);

  return;
}
#endif

void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

#if defined(CS333_P4)
#define HEADER "\nPID\tName\t\tUID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P3)
#define HEADER "\nPID\tName\t\tUID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName\t\tUID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName\t\tElapsed\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif

  cprintf(HEADER);  // not conditionally compiled as must work in all project states

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    // see TODOs above this function
#if defined(CS333_P4)
    procdumpP4(p, state);
#elif defined(CS333_P3)
    procdumpP3(p, state);
#elif defined(CS333_P2)
    procdumpP2(p, state);
#elif defined(CS333_P1)
    procdumpP1(p, state);
#else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif

    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
#ifdef CS333_P1
  cprintf("$ ");  // simulate shell prompt
#endif // CS333_P1
}

#if defined(CS333_P3)
// list management helper functions
static void
stateListAdd(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL){
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  } else{
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}
#endif

#if defined(CS333_P3)
static int
stateListRemove(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL || (*list).tail == NULL || p == NULL){
    return -1;
  }

  struct proc* current = (*list).head;
  struct proc* previous = 0;

  if(current == p){
    (*list).head = ((*list).head)->next;
    // prevent tail remaining assigned when we've removed the only item
    // on the list
    if((*list).tail == p){
      (*list).tail = NULL;
    }
    return 0;
  }

  while(current){
    if(current == p){
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found. return error
  if(current == NULL){
    return -1;
  }

  // Process found.
  if(current == (*list).tail){
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  } else{
    previous->next = current->next;
  }

  // Make sure p->next doesn't point into the list.
  p->next = NULL;

  return 0;
}
#endif

#if defined(CS333_P3)
static void
initProcessLists()
{
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++) {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
#if defined(CS333_P4)
  for (i = 0; i <= MAXPRIO; i++) {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
#endif
}
#endif

#if defined(CS333_P3)
static void
initFreeList(void)
{
  struct proc* p;

  for(p = ptable.proc; p < ptable.proc + NPROC; ++p){
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}
#endif

#if defined(CS333_P3)
// example usage:
// assertState(p, UNUSED, __FUNCTION__, __LINE__);
// This code uses gcc preprocessor directives. For details, see
// https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html
static void
assertState(struct proc *p, enum procstate state, const char * func, int line)
{
    if (p->state == state)
      return;
    cprintf("Error: proc state is %s and should be %s.\nCalled from %s line %d\n",
        states[p->state], states[state], func, line);
    panic("Error: Process state incorrect in assertState()");
}
#endif




#ifdef CS333_P2
int
getprocs(uint max, struct uproc *table)
{
  if(max > NPROC){
    if(max != 72)
      return -1;
  }

  acquire(&ptable.lock); 
  int i = 0;

  struct proc *p;
	
  for(int j = 0; j < max; j++){
    
    if(&ptable.proc[j] == NULL)
      break;
    p = &ptable.proc[j];
    if(p == NULL){
      break;
    }
    if(p->state != UNUSED && p->state != EMBRYO)
		{
			
      if(&table[i] == NULL)
        break;
			table[i].pid = p->pid;
	  	strncpy(table[i].name,p->name, sizeof(p->name)); 
			table[i].uid = p->uid;
			if(p->parent != NULL) 
				table[i].ppid = p->parent->pid;
			else
				table[i].ppid = p->pid;

			table[i].CPU_total_ticks = p->cpu_ticks_total;
			table[i].elapsed_ticks = p->cpu_ticks_in;
			strncpy(table[i].state,states[p->state], sizeof(p->name));
			table[i].size = p->sz;
			i++;
		} 
  }

  /*
  struct proc *p;
	struct uproc *tmp;
	
	for(p = ptable.proc; p < ptable.proc + NPROC; ++p){

		if(p->state != UNUSED && p->state != EMBRYO)
		{
			tmp = &table[i];
			tmp->pid = p->pid;
	  	strncpy(tmp->name,p->name, STRMAX); 
			tmp->uid = p->uid;
			if(p->parent != NULL) 
				tmp->ppid = p->parent->pid;
			else
				tmp->ppid = p->pid;

			tmp->CPU_total_ticks = p-> cpu_ticks_total;
			tmp->elapsed_ticks = p->cpu_ticks_in;
			strncpy(tmp->state,states[p->state], STRMAX);
			tmp->size = p->sz;
			i++;
		}
    
  }
  */

  release(&ptable.lock);
	return i;
}
#endif // end of P2


//dumps for control-r s z f

#ifdef CS333_P3
void
readydump(void)
{
	acquire(&ptable.lock);

  struct proc *cur = NULL;

  cprintf("Ready List Processes:\n"); 
	cur = ptable.list[RUNNABLE].head;
	if (cur == NULL){
     cprintf("Ready List is Empty\n");
		 release(&ptable.lock);
		 return;
	}


	while (cur != NULL){
		if (cur->next == NULL) 
			cprintf("%d\n", cur->pid);
		else
			cprintf("%d -> ", cur->pid);

		cur = cur->next;
	}
	cprintf("\n");	
  release(&ptable.lock);
  return;
}

void
freedump(void)
{
  struct proc *p;
  int numproc = 0;
  acquire(&ptable.lock); 
  for(p=ptable.list[UNUSED].head; p; p=p->next){
    assertState(p,UNUSED, __FUNCTION__, __LINE__);
    numproc++;
  }
  cprintf("Free List Size: %d processes\n",numproc);
  release(&ptable.lock);
  return;
}
void
sleepdump(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Sleep List Processes:\n");
  if(ptable.list[SLEEPING].head == NULL){
    cprintf("Sleep List is Empty\n");
    release(&ptable.lock);
    return;
  }
  for(p=ptable.list[SLEEPING].head; p; p=p->next){
    assertState(p,SLEEPING,__FUNCTION__, __LINE__);
    cprintf("%d",p->pid);
    if(p->next)
      cprintf("->");
  }
  release(&ptable.lock);
  cprintf("\n");
}

void
zombiedump(void)
{
	struct proc *p;
  acquire(&ptable.lock);
  cprintf("Zombie List Processes:\n");
  if(ptable.list[ZOMBIE].head == NULL){
    cprintf("Zombie List is Empty\n");
    release(&ptable.lock);
    return;
  }
  for(p=ptable.list[ZOMBIE].head; p; p=p->next){
    assertState(p,ZOMBIE,__FUNCTION__, __LINE__); 
    cprintf("(%d , %d)",p->pid, p->parent->pid);
    if(p->next)
      cprintf("->");
  }
	cprintf("\n");
  release(&ptable.lock);
}


#endif


#ifdef CS333_P4
int setpriority(int pid, int priority)
{
  return -1;
}

int getpriority(int pid)
{
  return -1;
}

#endif