#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include "float.h"
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/float.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/*Lista de todas as threads bloqueadas*/
static struct list lista_threads_bloqueadas;
/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
  void *eip;             /* Return address. */
  thread_func *function; /* Function to call. */
  void *aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
#define TIMER_FREQ 100        /* # Quantidade de ticks em um segundo*/
static unsigned thread_ticks; /* # of timer ticks since last yield. */
int avg = 0;                  /*Variavel global para guardar o valor de AVG*/

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static bool is_thread(struct thread *) UNUSED;
static void *alloc_frame(struct thread *, size_t size);
static void schedule(void);
void thread_schedule_tail(struct thread *prev);
static tid_t allocate_tid(void);

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
void thread_init(void)
{
  ASSERT(intr_get_level() == INTR_OFF);

  list_init(&lista_threads_bloqueadas); // Inicializa a lista de threads bloqueadas
  lock_init(&tid_lock);
  list_init(&ready_list);
  list_init(&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread();
  init_thread(initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid();

  initial_thread->cpu_time = 0; //Inici
  initial_thread->nice = 0;
}
/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init(&idle_started, 0);
  thread_create("idle", PRI_MIN, idle, &idle_started);

  avg = 0;
  /* Start preemptive thread scheduling. */
  intr_enable();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down(&idle_started);
}
/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
   
void thread_tick(void)
{
  struct thread *t = thread_current();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /*Essa parte verifica se o sistema de escalonamento de threads está utilizando o MLFQS (Escalonador Multinível com Fila de Feedback),
   que é um tipo de escalonador baseado em prioridades dinâmicas, para garantir a corretude do programa é necessario tal verificação*/

  if (thread_mlfqs) // Se for a thread de escalonamento MLFQS, então deve entrar
  {

    if (t != idle_thread)
      t->cpu_time = FLOAT_ADD_MIX(t->cpu_time, 1); //(float + int)

    /*Aqui, se a thread atual (t) não for a idle thread (a thread que fica em execução quando não há outras threads prontas), o tempo de CPU dessa thread é atualizado.*/

    /*TIMER_FREQ é uma constante que define a frequência do timer (provavelmente em ticks por segundo).
    A linha verifica se o número de ticks é múltiplo de TIMER_FREQ, ou seja, ela verifica se chegou a um ponto em que deve ser realizada uma ação periódica
    No caso devemos atualizar o tempo de CPU e o valor numerico do AVG a cada 100 Ticks*/
    if (timer_ticks() % TIMER_FREQ == 0) // A cada 100 ticks ele atualizar o avg e o tempo de cpu de cada thread
    {
      avg_load();   //Calculo de avg 
      thread_set_recent_cpu(); //Calcula o tempo de cpu de cada thread
    }
    if (timer_ticks() % TIME_SLICE == 0) // A cada 4 ticks ele atualiza a prioridade das threads
      set_priority();
  }
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return();
}
/* Prints thread statistics. */
void thread_print_stats(void)
{
  printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
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
tid_t thread_create(const char *name, int priority,
                    thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT(function != NULL);

  /* Allocate thread. */
  t = palloc_get_page(PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread(t, name, priority);
  tid = t->tid = allocate_tid();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame(t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame(t, sizeof *ef);
  ef->eip = (void (*)(void))kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame(t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock(t);

  // Garante que a thread atual seja preemptivamente "destruída" (ou seja, perca a CPU) se houver uma thread com prioridade mais alta pronta para ser executada.
  if (!list_empty(&ready_list) && t->priority < list_entry(list_front(&ready_list), struct thread, allelem)->priority)
    thread_yield();

  return tid;
}
/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void)
{
  ASSERT(!intr_context());
  ASSERT(intr_get_level() == INTR_OFF);

  thread_current()->status = THREAD_BLOCKED;
  schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
  enum intr_level old_level;

  ASSERT(is_thread(t));

  old_level = intr_disable();
  ASSERT(t->status == THREAD_BLOCKED);
  // list_push_back(&ready_list, &t->elem);
  
  struct list_elem *elemento; //Cria uma variavel do tipo elemento
  for (elemento = list_begin(&ready_list); elemento != list_end(&ready_list); elemento = list_next(elemento))
  {//Para cada elemento ele faz a iteração

    //Transforma o elemento da lista de um list_elem para um thread pra acessar os seus atributos
    struct thread *thread_lista = list_entry(elemento, struct thread, elem);
    if (t->priority > thread_lista->priority) //Se a prioridade da thread atual for maior que a da lista, da um break, pois ja achamos a posição correta para inserir
    {
      break;
    }
  }
  list_insert(elemento, &t->elem); //Insere a thread na posição correta por ordem de prioridade

  t->status = THREAD_READY;
  intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
  return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
  struct thread *t = running_thread();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT(is_thread(t));
  ASSERT(t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
  return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
  ASSERT(!intr_context());

#ifdef USERPROG
  process_exit();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable();
  list_remove(&thread_current()->allelem);
  thread_current()->status = THREAD_DYING;
  schedule();
  NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{
  struct thread *cur = thread_current();
  enum intr_level old_level;

  ASSERT(!intr_context());

  old_level = intr_disable();
  // list_push_back(&ready_list, &cur->elem);
  
  if (cur != idle_thread)
  {
    struct list_elem *elemento; //Cria uma variavel do tipo elemento
    for (elemento = list_begin(&ready_list); elemento != list_end(&ready_list); elemento = list_next(elemento))
    {//Para cada elemento ele faz a iteração
      
      //Transforma o elemento da lista de um list_elem para um thread pra acessar os seus atributos
      struct thread *thread_lista = list_entry(elemento, struct thread, elem);
      //Se a prioridade da thread atual for maior que a da lista, da um break, pois ja achamos a posição correta para inserir
      if (cur->priority > thread_lista->priority)
      {
        break;
      }
    }
    list_insert(elemento, &cur->elem); //Insere a thread na posição correta por ordem de prioridade
  }
  cur->status = THREAD_READY;
  schedule();
  intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach(thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT(intr_get_level() == INTR_OFF);

  for (e = list_begin(&all_list); e != list_end(&all_list);
       e = list_next(e))
  {
    struct thread *t = list_entry(e, struct thread, allelem);
    func(t, aux);
  }
}

//funçao para o calculo da prioridade
void set_priority(){
  struct list_elem *elemento;
  for (elemento = list_begin(&all_list); elemento != list_end(&all_list); elemento = list_next(elemento)) //loop para iterar sobre cada thread
  {
    struct thread *thread_lista = list_entry(elemento, struct thread, allelem);

    if (thread_lista != idle_thread)
    {
      //formula para calcular a nova prioridade da thread
      int new_priority = FLOAT_INT_PART(FLOAT_ADD(FLOAT_DIV_MIX(thread_lista->cpu_time, -4), FLOAT_CONST(PRI_MAX - (thread_lista->nice * 2))));

      //limita a prioridade entre PRI_MIN e PRI_MAX
      if (thread_lista->priority > PRI_MAX) //se a prioridade for maior que a maxima permitida, "trava" ela na max
        thread_lista->priority = PRI_MAX;
      else if (thread_lista->priority < PRI_MIN) //mesma coisa para a minima, se for menor do que a minima, "trava" na min
        thread_lista->priority = PRI_MIN;
      else
        thread_lista->priority = new_priority;  //caso contrario, esta tudo certo
    }
  }
}

/*Sets the current thread's priority to NEW_PRIORITY*/
void thread_set_priority(int new_priority)
{
  // Se o sistema estiver rodando o MLFQS, a prioridade das threads é gerenciada automaticamente,
  if (!thread_mlfqs)
  {
    // Obtém um ponteiro para a thread atualmente em execução
    struct thread *t = thread_current();
    t->priority = new_priority; //Defina a nova prioridade
    //Chama `thread_yield()` para verificar se a thread ainda pode continuar executando.
    // Se existir outra thread com prioridade maior, a CPU será cedida a ela.
    thread_yield();
  }
}
/* Returns the current thread's priority. */
int thread_get_priority(void)
{
  return thread_current()->priority;
}
/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice)
{
  struct thread *t = thread_current(); //ponteiro que aponta para a thread atual
  t->nice = nice; // Atribuicao do nice
  int priority = nova_Prioridade(t); //atribuicao da prioridade

  if (priority > PRI_MAX) 
    t->priority = PRI_MAX; //se a prioridade for maior que a maxima permitida, "trava" ela na max
  else if(priority < PRI_MIN)
    t->priority = PRI_MIN; //mesma coisa para a minima, se for menor do que a minima, "trava" na min
  else                     
    t->priority = priority; //caso contrario, esta tudo certo

  // Garante que a thread atual seja preemptivamente "destruída" (ou seja, perca a CPU) se houver uma thread com prioridade mais alta pronta para ser executada.
  if (!list_empty(&ready_list) && t->priority < list_entry(list_front(&ready_list), struct thread, allelem)->priority)
    thread_yield();
}
/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
  return thread_current()->nice;
}
/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
  // return FLOAT_ROUND(avg*100);
  // return FLOAT_ROUND(avg)*100;
  int load_avg_value = FLOAT_INT_PART(FLOAT_MULT_MIX(avg, 100)); // (NOVO) Pega o valor de load average atual.

  return load_avg_value; // (NOVO) retorna 100 vezes o valor de load average atual
}

void thread_set_recent_cpu(void)
{
  struct list_elem *elemento;
  //loop para iterar sobre cada thread
  for (elemento = list_begin(&all_list); elemento != list_end(&all_list); elemento = list_next(elemento))
  {
    struct thread *thread_lista = list_entry(elemento, struct thread, allelem); //cria uma lista para alocar as threads
    thread_lista->cpu_time = (FLOAT_ADD_MIX(FLOAT_MULT(FLOAT_DIV(FLOAT_MULT_MIX(avg, 2), FLOAT_ADD_MIX(FLOAT_MULT_MIX(avg, 2), 1)), thread_lista->cpu_time), thread_lista->nice));
    // ^^
    // formula para calcular recent CPU: CpuTime = ( ( (2*avg)/(2*avg+1) ) * CpuTime + nice)
  }
}
/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
  return FLOAT_INT_PART(FLOAT_MULT_MIX(thread_current()->cpu_time, 100)); //retorna o recentCpu da thread atual *100
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
idle(void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current();
  sema_up(idle_started);

  for (;;)
  {
    /* Let someone else run. */
    intr_disable();
    thread_block();

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
    asm volatile("sti; hlt" : : : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
  ASSERT(function != NULL);

  intr_enable(); /* The scheduler runs with interrupts off. */
  function(aux); /* Execute the thread function. */
  thread_exit(); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread(void)
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm("mov %%esp, %0" : "=g"(esp));
  return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread(struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT(t != NULL);
  ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT(name != NULL);

  memset(t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy(t->name, name, sizeof t->name);
  t->stack = (uint8_t *)t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  t->cpu_time = 0;
  t->nice = 0;

  old_level = intr_disable();
  list_push_back(&all_list, &t->allelem);
  intr_set_level(old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame(struct thread *t, size_t size)
{
  /* Stack data is always allocated in word-size units. */
  ASSERT(is_thread(t));
  ASSERT(size % sizeof(uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
  if (list_empty(&ready_list))
    return idle_thread;
  else
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

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
void thread_schedule_tail(struct thread *prev)
{
  struct thread *cur = running_thread();

  ASSERT(intr_get_level() == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
  {
    ASSERT(prev != cur);
    palloc_free_page(prev);
  }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule(void)
{
  struct thread *cur = running_thread();
  struct thread *next = next_thread_to_run();
  struct thread *prev = NULL;

  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(cur->status != THREAD_RUNNING);
  ASSERT(is_thread(next));

  if (cur != next)
  {
    prev = switch_threads(cur, next);
  }
  thread_schedule_tail(prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire(&tid_lock);
  tid = next_tid++;
  lock_release(&tid_lock);

  return tid;
}
/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);

void thread_dormir(int64_t ticks)
{
  if (ticks <= 0)  //Verifica se o momento de acordar(ticks) é valido
    return;

  enum intr_level estado_anterior; //Guarda o estado anterior
  ASSERT(!intr_context()); //Verifica se não esta em um contexto de interrupção
  estado_anterior = intr_disable(); //Desabilita as interrupções e guarda na variavel

  struct thread *thread_atual = thread_current(); //Guarda a thread_atual numa variavel


  if (thread_atual != idle_thread) // Se a thread for diferente da idle_thread
  {
    thread_atual->momento_de_acordar = ticks; //Defino o momento de acordar 

    struct list_elem *elemento;//Crio uma variavel elemento
    for (elemento = list_begin(&lista_threads_bloqueadas); elemento != list_end(&lista_threads_bloqueadas); elemento = list_next(elemento))
    {//Percorre os elementos da lista 

      struct thread *thread_lista = list_entry(elemento, struct thread, elem);//Transforma o elemento numa thread
      if (thread_atual->momento_de_acordar < thread_lista->momento_de_acordar) 
      { //Se o momento de acordar da thread atual for menor que o elemento da lista comparada, então insere ele
        list_insert(elemento, &thread_atual->elem);
        break;
      }
    }
    if (elemento == list_end(&lista_threads_bloqueadas)) //Caso não foi inserida ainda, então insere no final da lista
      list_push_back(&lista_threads_bloqueadas, &thread_atual->elem);

    thread_block(); //Bloqueia a thread, ou seja dorme a thread
  }
  intr_set_level(estado_anterior);//Seta o estado anterior antes da interrupção
}

void thread_acordar(void)
{

  if (list_empty(&lista_threads_bloqueadas)) //Se a lista de threads bloqueadas for vazia, então ja fecha
    return;

  enum intr_level estado_anterior;//Estado anterior
  struct thread *thread_lista; //Variavel thread_lista
  struct list_elem *thread_atual = list_begin(&lista_threads_bloqueadas); //Thread_atual como primeiro elemento da lista_bloqueadas
  struct list_elem *proxima_thread;//Variavel para a proxima_thread
  while (thread_atual != list_end(&lista_threads_bloqueadas)) //Percorre cada elemento da lista de threas bloqueadas
  {
    thread_lista = list_entry(thread_atual, struct thread, elem); 
    if (thread_lista->momento_de_acordar > timer_ticks()) //Se o momento de acordar for maior, então não chegou o momento de acordar
      break;

    //Caso tenha passado,então ja chegou o momento de acordar
    enum intr_level estado_anterior = intr_disable(); //Desabilita as interrupções
    proxima_thread = list_next(thread_atual); //Pega a proxima threas
    list_remove(thread_atual); //Remove a thread atual da lista de bloqueadas
    thread_unblock(thread_lista); //Desbloqueia ela
    intr_set_level(estado_anterior); //Volta ao estado anterior antes da interrupção

    thread_atual = proxima_thread; //Atualiza a proxima thread
  }
}
// Função que calcula o novo valor de load_avg
void avg_load(void)
{
  int tamanho_lista = list_size(&ready_list); // Tamanho da lista de threads prontas
  if (thread_current() != idle_thread)    // Se a thread atual não for a idle thread
    tamanho_lista++;                      // Incrementa o tamanho da lista

  // atualizacao do valor de load_avg
  //formula: load_avg = (59/60)*load_avg + (1/60)*ready_threads
  avg = FLOAT_ADD(FLOAT_MULT(FLOAT_DIV_MIX(FLOAT_CONST(59), 60), avg), FLOAT_MULT_MIX(FLOAT_DIV_MIX(FLOAT_CONST(1), 60), tamanho_lista)); // calculo do novo valor de load_avg
}

 // Função que calcula a nova prioridade de uma thread
int nova_Prioridade(struct thread *thread_atual)
{

  // Calculo da nova prioridade
  int nova_prioridade = FLOAT_INT_PART(FLOAT_ADD(FLOAT_DIV_MIX(thread_atual->cpu_time, -4), FLOAT_CONST(PRI_MAX - (thread_atual->nice * 2))));

  // Limita a prioridade entre PRI_MIN e PRI_MAX
  if (nova_prioridade > PRI_MAX)
    nova_prioridade = PRI_MAX;
  else if(nova_prioridade < PRI_MIN)
    nova_prioridade = PRI_MIN;

  return nova_prioridade; // Retorna a nova prioridade
}
