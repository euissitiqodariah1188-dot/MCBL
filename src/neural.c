#include "neural.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <windows.h>
  #define MCBL_SLEEP_US(us) Sleep((us) / 1000)
#else
  #include <time.h>
  #define MCBL_SLEEP_US(us) do { \
      struct timespec _ts = {0, (us) * 1000}; \
      nanosleep(&_ts, NULL); \
  } while(0)
#endif

/* -----------------------------------------------------------------------
   McBL# Neural Network Task Distributor implementation
   20-core ring with work-stealing
   ----------------------------------------------------------------------- */

typedef struct {
    NeuralNetwork *nn;
    int            core_id;
    pthread_t      thread;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             running;
} CoreThread;

static CoreThread g_threads[NEURAL_CORE_COUNT];
static int        g_threads_init = 0;

static NeuralCore *least_busy(NeuralNetwork *nn) {
    NeuralCore *best = &nn->cores[0];
    for (int i = 1; i < NEURAL_CORE_COUNT; i++) {
        if (nn->cores[i].count < best->count)
            best = &nn->cores[i];
    }
    return best;
}

static void process_task(NeuralNetwork *nn, NeuralTask *t) {
    if (!t || t->done) return;
    nn->tasks_completed++;

    switch (t->kind) {
        case TASK_COMPILE_INC:
        case TASK_COMPILE_DEV:
        case TASK_EXEC_BYTECODE:
        case TASK_ANALYSE_AST:
        case TASK_OPTIMISE:
            /* delegate execution to callback if provided */
            if (t->callback) t->callback(t->data, t->user_data);
            break;
        case TASK_IDLE:
        default:
            break;
    }
    t->done = 1;
}

static void *core_thread_fn(void *arg) {
    CoreThread    *ct = (CoreThread *)arg;
    NeuralNetwork *nn = ct->nn;
    NeuralCore    *core = &nn->cores[ct->core_id];

    while (ct->running) {
        NeuralTask *task = NULL;

        pthread_mutex_lock(&ct->mutex);
        while (core->count == 0 && ct->running)
            pthread_cond_wait(&ct->cond, &ct->mutex);

        if (core->count > 0) {
            task = &core->queue[core->head];
            core->head  = (core->head + 1) % NEURAL_QUEUE_SIZE;
            core->count--;
        }
        pthread_mutex_unlock(&ct->mutex);

        if (task) {
            core->busy = 1;
            process_task(nn, task);
            core->busy = 0;

            /* work stealing: if next core is empty, take one of its tasks */
            NeuralCore *next_core = &nn->cores[core->next_core_id];
            if (next_core->count > 0 && !next_core->busy) {
                pthread_mutex_lock(&g_threads[core->next_core_id].mutex);
                if (next_core->count > 0) {
                    NeuralTask stolen = next_core->queue[next_core->head];
                    next_core->head  = (next_core->head + 1) % NEURAL_QUEUE_SIZE;
                    next_core->count--;
                    pthread_mutex_unlock(&g_threads[core->next_core_id].mutex);
                    process_task(nn, &stolen);
                } else {
                    pthread_mutex_unlock(&g_threads[core->next_core_id].mutex);
                }
            }
        }
    }
    return NULL;
}

NeuralNetwork *neural_create(void) {
    NeuralNetwork *nn = (NeuralNetwork *)mcbl_calloc(1, sizeof(NeuralNetwork));

    /* Init cores with ring topology */
    for (int i = 0; i < NEURAL_CORE_COUNT; i++) {
        nn->cores[i].core_id      = i;
        nn->cores[i].head         = 0;
        nn->cores[i].tail         = 0;
        nn->cores[i].count        = 0;
        nn->cores[i].busy         = 0;
        nn->cores[i].next_core_id = (i + 1) % NEURAL_CORE_COUNT;
        nn->cores[i].prev_core_id = (i - 1 + NEURAL_CORE_COUNT) % NEURAL_CORE_COUNT;
    }
    nn->round_robin      = 0;
    nn->tasks_dispatched = 0;
    nn->tasks_completed  = 0;

    /* Spawn worker threads */
    if (!g_threads_init) {
        for (int i = 0; i < NEURAL_CORE_COUNT; i++) {
            g_threads[i].nn      = nn;
            g_threads[i].core_id = i;
            g_threads[i].running = 1;
            pthread_mutex_init(&g_threads[i].mutex, NULL);
            pthread_cond_init (&g_threads[i].cond,  NULL);
            pthread_create(&g_threads[i].thread, NULL, core_thread_fn, &g_threads[i]);
        }
        g_threads_init = 1;
    }

    return nn;
}

void neural_destroy(NeuralNetwork *nn) {
    if (!nn) return;
    neural_flush(nn);

    if (g_threads_init) {
        for (int i = 0; i < NEURAL_CORE_COUNT; i++) {
            pthread_mutex_lock(&g_threads[i].mutex);
            g_threads[i].running = 0;
            pthread_cond_signal(&g_threads[i].cond);
            pthread_mutex_unlock(&g_threads[i].mutex);
        }
        for (int i = 0; i < NEURAL_CORE_COUNT; i++) {
            pthread_join(g_threads[i].thread, NULL);
            pthread_mutex_destroy(&g_threads[i].mutex);
            pthread_cond_destroy (&g_threads[i].cond);
        }
        g_threads_init = 0;
    }

    mcbl_free((void **)&nn);
}

int neural_submit(NeuralNetwork *nn, NeuralTaskKind kind,
                  void *data,
                  void (*callback)(void *result, void *user_data),
                  void *user_data) {
    if (!nn) return -1;

    /* Choose least-busy core */
    NeuralCore *core = least_busy(nn);
    int cid = core->core_id;

    pthread_mutex_lock(&g_threads[cid].mutex);

    if (core->count >= NEURAL_QUEUE_SIZE) {
        pthread_mutex_unlock(&g_threads[cid].mutex);
        fprintf(stderr, "McBL# Neural: core %d queue full\n", cid);
        return -1;
    }

    NeuralTask *t = &core->queue[core->tail];
    t->kind      = kind;
    t->data      = data;
    t->callback  = callback;
    t->user_data = user_data;
    t->done      = 0;

    core->tail  = (core->tail + 1) % NEURAL_QUEUE_SIZE;
    core->count++;
    nn->tasks_dispatched++;

    pthread_cond_signal(&g_threads[cid].cond);
    pthread_mutex_unlock(&g_threads[cid].mutex);

    return 0;
}

void neural_flush(NeuralNetwork *nn) {
    if (!nn) return;
    /* Drain: wait until all cores have empty queues */
    int retries = 0;
    while (retries < 10000) {
        int all_idle = 1;
        for (int i = 0; i < NEURAL_CORE_COUNT; i++) {
            if (nn->cores[i].count > 0 || nn->cores[i].busy) {
                all_idle = 0;
                break;
            }
        }
        if (all_idle) break;
        MCBL_SLEEP_US(100);
        retries++;
    }
}

void neural_stats(const NeuralNetwork *nn) {
    if (!nn) return;
    printf("McBL# Neural Network stats:\n");
    printf("  Cores            : %d\n", NEURAL_CORE_COUNT);
    printf("  Tasks dispatched : %zu\n", nn->tasks_dispatched);
    printf("  Tasks completed  : %zu\n", nn->tasks_completed);
    for (int i = 0; i < NEURAL_CORE_COUNT; i++) {
        const NeuralCore *c = &nn->cores[i];
        printf("  Core[%02d] queue=%d busy=%d next=%d prev=%d\n",
               i, c->count, c->busy, c->next_core_id, c->prev_core_id);
    }
}
