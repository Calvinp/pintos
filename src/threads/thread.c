#include "devices/timer.h"
#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of processes in THREAD_SLEEPING state sorted by time until
   wake-up. Processes are added to this list when they fall asleep. */
static struct list sleeping_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* load_avg, system load average, estimates the average number of threads 
   ready to run over the past minute. */
static intn14_t load_avg; /* ADDED BY US */

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *t, const char *name, int nice, intn14_t recent_cpu, int  priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&sleeping_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", 0, 0, PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
    thread_increment_recent_cpu();
#endif
  else
    kernel_ticks++;
    thread_increment_recent_cpu();

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, thread_get_nice(), thread_get_recent_cpu(), priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);
  thread_yield();
  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);
  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, &max_effective_priority_thread, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, &max_effective_priority_thread, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}


/* Compare the wake time of the two threads given by the list_elem. */
bool compare_wake_time(const struct list_elem* a, const struct list_elem* b, void *aux UNUSED){
	struct thread *ta = list_entry (a, struct thread, sleepelem);
	struct thread *tb = list_entry (b, struct thread, sleepelem);
	return ta->wake_time < tb->wake_time;	
}


/* Puts the current thread to sleep for given number of ticks. */
void thread_sleep (int64_t ticks) {
	struct thread *cur = thread_current();
	enum intr_level old_level;

	old_level = intr_disable();
	if (cur != idle_thread) {
		cur->status = THREAD_SLEEPING;
		cur->wake_time = timer_ticks() + ticks;
		list_insert_ordered (&sleeping_list, &cur->sleepelem, &compare_wake_time, &(cur->wake_time));	
		schedule();
	}
	intr_set_level (old_level);
}

/* 
 * Some integer n.14 math functions 
 * Some of these functions are easy, but their use guaruntees addition of int n.14s.
 * Simply using +, the compiler won't catch the int + intn14_t bug.
 */

/* Converts an integer into an n.14 integer */
intn14_t intToIntn14(int a) {
  return a * (FPOINT_CONST);
}

/* Converts an n.14 integer into an integer */ 
int intn14ToInt(intn14_t a) {
  if (a > 0) {
    return (a + (FPOINT_CONST)/2) / (FPOINT_CONST);
  } else {
    return (a - (FPOINT_CONST)/2) / (FPOINT_CONST);
  }
}

/* Adds two n.14 integers, a+b */
intn14_t add_n14(intn14_t a, intn14_t b) {
  return a + b;
}

/* Subtracts two  n.14 integers, a-b */
intn14_t sub_n14(intn14_t a, intn14_t b) {
  return a - b;
}

/* Multiplies two n.14 integers, a*b */
/* NOTE - By our typedef, intn14_t is already int64_t, so doesn't need to be converted */
intn14_t mult_n14(intn14_t a, intn14_t b) {
  return (a * b)/(FPOINT_CONST);
}

/* Divides two n.14 integers, a/b */
intn14_t div_n14(intn14_t a, intn14_t b) {
  ASSERT(b != 0);
  return (a * (FPOINT_CONST))/b;
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  struct thread *t = thread_current();
  t->priority = new_priority;
  thread_recalculate_effective_priority(t);
  thread_yield();
}


void thread_donate_priority (struct thread *donor, struct thread *receiver, int depth) { // To donate priority to a thread:
  struct thread *d = donor;
  struct thread *r = receiver;
  struct lock *loc = d->lock_waiting_on;
  ASSERT (intr_get_level () == INTR_OFF); // Interrupts must be off
  
  while (loc != NULL && r->effective_priority < d->effective_priority && depth < MAX_DEPTH) {
    ASSERT (d != r); // Donor must not be donating to itself
    list_try_remove(&d->donor_elem);
    list_insert_ordered(&r->donors_list, &d->donor_elem, &max_effective_priority_thread, NULL); // Put donor in the list of threads donating to reciever
    r->effective_priority = d->effective_priority; // Give the donating thread's priority to the recieving thread, unless it already has higher priority than us
    if (r->status == THREAD_READY) { // If the recieving thread is ready to run...
      list_remove(&r->elem); // ... remove it from the ready list and put it back on in its new position
      list_insert_ordered(&ready_list, &r->elem, &max_effective_priority_thread, NULL);
    }
    
    loc = r->lock_waiting_on;
    d = r;
    if (loc != NULL) {
     r = r->lock_waiting_on->holder;
    }
    depth++;
  }
}

/* Recalculates the thread priority just before it releases the lock. */ /* ADDED BY US */
void thread_calculate_effective_priority(struct lock *loc){
 
  ASSERT (intr_get_level () == INTR_OFF);

  struct thread *loc_holder = thread_current();
  struct list *donors_list = &loc_holder->donors_list;
  struct list_elem *e = list_begin(donors_list);

  if(!list_empty(donors_list)){
  
		while ( e != list_end(donors_list)) {
		 
      struct thread *t = list_entry (e, struct thread, donor_elem);
      ASSERT (t->magic == THREAD_MAGIC);
		  if (t->lock_waiting_on == loc) {
        e = list_remove(e);
		  } else {
		    e = list_next(e);
		  }	  
		}


		 struct thread *donors_first_element = list_entry (list_begin(donors_list), struct thread, donor_elem);
     int new_priority = max(loc_holder->priority, donors_first_element->effective_priority);
     loc_holder->effective_priority = new_priority;
 
  }else{
 		 loc_holder->effective_priority = loc_holder->priority;
  }
 
}


/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->effective_priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  thread_current()->nice = nice;
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{ 
  return load_avg;
}

/* Set the system load average value. */ /* ADDED BY US */
void thread_set_load_avg(void){
  ASSERT( timer_ticks () % TIMER_FREQ == 0);
  intn14_t size = intToIntn14(list_size(&ready_list));
  load_avg = add_n14(mult_n14(fiftyNineOverSixty, load_avg), mult_n14(oneOverSixty, size));  
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return intn14ToInt(mult_n14(thread_current()->recent_cpu, oneHundred));
}

/* Increments the current thread's recent_cpu */ /* ADDED BY US */
void thread_increment_recent_cpu(void) {
  struct thread *t = thread_current();
  t->recent_cpu = add_n14(t->recent_cpu, one);
}

/* Recalculates the recent_cpu of every single thread */ /* ADDED BY US */
void recalculate_all_recent_cpu(void) {
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
          thread_recalculate_recent_cpu(t);
  }
}

/* Recalculates the recent cpu of the given thread */ /* ADDED BY US */
void thread_recalculate_recent_cpu(struct thread *t) {
  intn14_t recent_cpu = t->recent_cpu;
  intn14_t nice = intToIntn14(t->nice);
  intn14_t coeff = div_n14(mult_n14(two, load_avg), add_n14(mult_n14(two, load_avg), one));
  t->recent_cpu =  add_n14(mult_n14(coeff, recent_cpu), nice);
}

/* Recalculates the priority of every single thread */ /* ADDED BY US */
void recalculate_all_priority(void) {
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    if (t != idle_thread) {
      thread_recalculate_priority(t);
    }
  }
  list_sort(&ready_list, &max_effective_priority_thread, NULL);
}

/* Recalculates the priority of the given thread */ /* ADDED BY US */
void thread_recalculate_priority(struct thread *t) {
  intn14_t recent_cpu = t->recent_cpu;
  intn14_t nice = intToIntn14(t->nice);
  intn14_t max_pri = intToIntn14(PRI_MAX);
  intn14_t recentCpuOverFour = div_n14(recent_cpu, four);
  intn14_t twoNice = mult_n14(nice, two);
  int newPriority = intn14ToInt(sub_n14(sub_n14(max_pri, recentCpuOverFour), twoNice));
  if (newPriority > PRI_MAX) {
    newPriority = PRI_MAX;
  } else if (newPriority < PRI_MIN) {
    newPriority = PRI_MIN;
  }
  t->priority = newPriority;
  thread_recalculate_effective_priority(t);
}

/* Recalculates the thread's effective priority */
void thread_recalculate_effective_priority(struct thread *t) {
  struct thread *donor;
  donor = list_entry (list_begin(&t->donors_list), struct thread, donor_elem);
  t->effective_priority = max(t->priority, donor->effective_priority);
  if (t->status == THREAD_READY) {
    list_remove(&t->elem);
    list_insert_ordered(&ready_list, &t->elem, &max_effective_priority_thread, NULL);
  }
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int nice, intn14_t recent_cpu, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->nice = nice;
  t->recent_cpu = recent_cpu;
  t->magic = THREAD_MAGIC;
  t->priority = priority;
  t->effective_priority = priority;
  list_init(&t->donors_list);		/*ADDED BY US */
  
  old_level = intr_disable ();
  
  list_push_back (&all_list, &t->allelem);
  
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}


/* Search for the highest effective priority thread from the ready list *//* ADDED BY US*/
bool max_effective_priority_thread(const struct list_elem *a,const struct list_elem *b,void *aux UNUSED){
     struct thread *t_a = list_entry (a, struct thread, elem);
     struct thread *t_b = list_entry (b, struct thread, elem);

     return t_a->effective_priority > t_b->effective_priority;
}


/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else{
	return list_entry (list_pop_front (&ready_list), struct thread, elem);
  }
  
}
/* REMOVED THIS LINE : list_entry (list_max(&ready_list,&max_effective_priority_thread,void *aux) , struct thread, elem);*/

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/*  Checks if any sleeping thread is ready to wake up. A thread is
		ready to wake up if its wake_time is less than the current time,
		as given by timer_ticks(). If a sleeping thread is ready to wake
		up, change its status from THREAD_SLEEPING to THREAD_READY */
static void
wake_up_thread (void)
{
	
	if(list_empty(&sleeping_list)){
		return;
	}

	struct list_elem *e = list_begin (&sleeping_list);
	int64_t cur_ticks = timer_ticks();
	struct thread *t = list_entry (e, struct thread, sleepelem);

	if (cur_ticks >= t->wake_time) {
		list_insert_ordered (&ready_list, &t->elem, &max_effective_priority_thread, NULL); /* Wake this thread up! */
		t->status = THREAD_READY;
		list_remove(e);   /* Remove this thread from sleeping_list */
		wake_up_thread();    /* Wakes up any other sleeping thread whose wake time has passed */
	}
	
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  wake_up_thread();
  
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
