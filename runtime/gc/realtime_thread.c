/* http://stackoverflow.com/questions/3649281/how-to-increase-thread-priority-in-pthreads */

#include "realtime_thread.h"

#define LOCK(X) { MYASSERT(pthread_mutex_lock(&X), ==, 0); }
#define UNLOCK(X) { MYASSERT(pthread_mutex_unlock(&X), ==, 0); }

#define RESPRI  2
#define MAXPRI 10 /* 0 .. 10 */

static pthread_mutex_t thread_queue_lock;
static struct _TQ {
	GC_thread t;
	bool runnable;
} thread_queue[MAXPRI-2];

volatile bool RTThreads_beginInit = FALSE;
volatile int32_t RTThreads_initialized= 0;
volatile int32_t Proc_criticalCount;
volatile int32_t Proc_criticalTicket;

pthread_mutex_t AllocatedThreadLock;

int32_t GC_getThreadPriority(GC_state s, pointer p) {
	GC_thread gct;
	gct = (GC_thread)(objptrToPointer(s->currentThread, s->heap.start) + offsetofThread (s));

	return gct->prio;
}

/* TODO
 * lookup thread,
 * change prio in GC_thread struct,
 * move thread to new queue if nec'y
 * change the posix priority
 */
int32_t GC_setThreadPriority(GC_state s, pointer p, int32_t prio) {
	return prio;
}

/* TODO
 * move thread to back of queue
 * pthread_yield
 */
int32_t GC_threadYield(GC_state s) {
	return 0;
}

int RTThread_addThreadToQueue(GC_thread t, int priority) {
    fprintf(stderr, "addThreadToQueue(pri=%d)\n", priority);

	if (priority < 0 || priority == 1 || priority > MAXPRI) return -1;
	LOCK(thread_queue_lock);
	thread_queue[priority - RESPRI].runnable = TRUE;
	thread_queue[priority - RESPRI].t = t;
	UNLOCK(thread_queue_lock);
	return 0;
}

void RTThread_waitForInitialization (GC_state s) {
  int32_t unused;

  while (!RTThreads_beginInit) { }

  unused = __sync_fetch_and_add (&RTThreads_initialized, 1);

  while (!RTThread_isInitialized(s)) { }
  initProfiling (s);
}


bool RTThread_isInitialized (GC_state s) {
  return RTThreads_initialized == (MAXPRI-RESPRI);
}

void realtimeThreadInit(struct GC_state *state) {
	int rv = 0;

	rv = pthread_mutex_init(&thread_queue_lock, NULL);
	assert(rv == 0);
	memset(&thread_queue, 0, sizeof(struct _TQ)*(MAXPRI-2));

	pthread_t *realtimeThreads =
			malloc((MAXPRI-RESPRI) * sizeof(pthread_t));
	assert(realtimeThreads != NULL);

	state->realtimeThreadConts =
			malloc((MAXPRI-RESPRI) * sizeof(struct cont));

	//Initializing the locks for each realtime thread created,
	state->realtimeThreadLocks= malloc((MAXPRI-RESPRI) * sizeof(pthread_mutex_t));
        int tNum;
        for (tNum = 0; tNum < (MAXPRI-RESPRI); tNum++) {
	    fprintf(stderr, "spawning thread %d\n", tNum);

            struct realtimeRunnerParameters* params =
                malloc(sizeof(struct realtimeRunnerParameters));
            params->tNum = tNum;
            params->state = state;

            state->realtimeThreadConts[tNum].nextChunk = NULL;

            if (pthread_create(&realtimeThreads[tNum], NULL, &realtimeRunner,
                        (void*)params)) {
                fprintf(stderr, "pthread_create failed: %s\n", strerror (errno));
                exit (1);
            }
        }

        state->realtimeThreads = realtimeThreads;

        state->realtimeThreadAllocated =
            malloc((MAXPRI-RESPRI) * sizeof(bool));

        for (tNum = 0; tNum < (MAXPRI-RESPRI); tNum++) {
            state->realtimeThreadAllocated[tNum] = false;
        }
}

void* realtimeRunner(void* paramsPtr) {

    struct realtimeRunnerParameters *params = paramsPtr;

    while (1) {
        // Trampoline
        int tNum = params->tNum;
        printf("%x] realtimeRunner[%d] running.\n", pthread_self(), tNum);

	sleep(1);

        LOCK(thread_queue_lock);
        if ( thread_queue[params->tNum].runnable != TRUE ) {
        	printf("%x] pri %d has work to do\n", pthread_self(), tNum);
		}
		else {
			printf("%x] pri %d has nothing to do\n", pthread_self(), tNum);
		}
		UNLOCK(thread_queue_lock);

		// copy the cont struct to this local variable
		struct GC_state *state = params->state;

		// TODO lock lock[tNum]
		//Acquiring lock associated with pThread from GC state
		pthread_mutex_lock(&state->realtimeThreadLocks[tNum]);

        struct cont* realtimeThreadConts = state->realtimeThreadConts;

        struct cont cont = realtimeThreadConts[tNum];

        if (cont.nextChunk != NULL) {
            cont=(*(struct cont(*)(void))cont.nextChunk)();
            cont=(*(struct cont(*)(void))cont.nextChunk)();
            cont=(*(struct cont(*)(void))cont.nextChunk)();
            cont=(*(struct cont(*)(void))cont.nextChunk)();
            cont=(*(struct cont(*)(void))cont.nextChunk)();
            cont=(*(struct cont(*)(void))cont.nextChunk)();
            cont=(*(struct cont(*)(void))cont.nextChunk)();
            cont=(*(struct cont(*)(void))cont.nextChunk)();

            // copy local variable back to gcstate
            params->state->realtimeThreadConts[tNum] = cont;
	
        } 

        // TODO unlock lock[tNum]
	pthread_mutex_unlock(&state->realtimeThreadLocks[tNum]);
    }
    return NULL;
}

int allocate_pthread(struct GC_state *state, struct cont *cont) {

	//grab a lock for accessing the allocated threads structure

	pthread_mutex_lock(&AllocatedThreadLock);
	
	//Loop through allocate thread structure to find free spot
	for(int i = 0 ; i < (MAXPRI-RESPRI) ; i++)
	{
		if(!state->realtimeThreadAllocated[i])
		  {	
			pthread_mutex_lock(&state->realtimeThreadLocks[i]);
			state->realtimeThreadConts[i] = *cont;
			pthread_mutex_unlock(&state->realtimeThreadLocks[i]);
			
			//Mark thread as allocated
			state->realtimeThreadAllocated[i] = true;
			pthread_mutex_unlock(&AllocatedThreadLock);
			return i;
		  }	
	}
	
	pthread_mutex_unlock(&AllocatedThreadLock);
	return -1;

}