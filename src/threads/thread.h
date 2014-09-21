#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_SLEEPING,	/* New state for sleeping threads. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

typedef int64_t intn14_t; /* An integer in the n.14 format - the rightmost 14 bits are after the decimal */ /* ADDED BY US */

#define FPOINT_CONST 1<<14 /* Multiplication constant for n.14 integers */
intn14_t intToIntn14(int a); /* Converts an integer into an n.14 integer */
int intn14ToInt(intn14_t a); /* Converts an n.14 integer into an integer */
intn14_t add_n14(intn14_t a, intn14_t b); /* Adds two n.14 integers, a+b */
intn14_t sub_n14(intn14_t a, intn14_t b); /* Subtracts two  n.14 integers, a-b */
intn14_t mult_n14(intn14_t a, intn14_t b); /* Multiplies two n.14 integers, a*b */
intn14_t div_n14(intn14_t a, intn14_t b); /* Divides two n.14 integers, a/b */

/* Store these as constants to avoid calculating it over and over */
#define oneOverSixty div_n14(intToIntn14(1), intToIntn14(60))
#define fiftyNineOverSixty div_n14(intToIntn14(59), intToIntn14(60)) 
#define one intToIntn14(1) 
#define two intToIntn14(2)
#define four intToIntn14(4) 
#define oneHundred intToIntn14(100)

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    intn14_t recent_cpu;								/* CPU time received by the thread recently. */ /* ADDED BY US */
    int nice;												 		/* Nice value of thread. */ /* ADDED BY US */
    struct list_elem allelem;           /* List element for all threads list. */
    struct list_elem sleepelem;         /* List element for sleeping list. */
    struct list_elem donorselem;        /* List element for donors list */
    int64_t wake_time;								  /* If I am asleep, when I have to wake up */ 
    int effective_priority;							/* priority of thread calculated */
    struct list donors_list;                 /* Threads donating to us */ /* ADDED BY US */
    

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

void thread_sleep(int64_t);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
void thread_increment_recent_cpu(void);
void recalculate_all_recent_cpu(void);
void thread_recalculate_recent_cpu(struct thread*);
void recalculate_all_priority(void);
void thread_recalculate_priority(struct thread *t);
void thread_recalculate_effective_priority(struct thread *t);
int thread_get_load_avg (void);
void thread_set_load_avg(void);
void thread_accept_priority (struct thread *donee);

bool compare_wake_time(const struct list_elem*, const struct list_elem*, void *aux);
bool max_effective_priority_thread(const struct list_elem *a,const struct list_elem *b,void *aux);

#endif /* threads/thread.h */
