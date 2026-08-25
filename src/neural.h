#ifndef MCBL_NEURAL_H
#define MCBL_NEURAL_H

#include "ast.h"
#include <stddef.h>

/* -----------------------------------------------------------------------
   McBL# Neural Network Task Distributor
   20 cores interconnected in a ring topology.
   Routes work units across cores for parallel compilation/execution.
   ----------------------------------------------------------------------- */

#define NEURAL_CORE_COUNT  20
#define NEURAL_QUEUE_SIZE  256

typedef enum {
    TASK_COMPILE_INC,    /* compile an inc block      */
    TASK_COMPILE_DEV,    /* compile a dev function    */
    TASK_EXEC_BYTECODE,  /* execute a chunk in MDK VM */
    TASK_ANALYSE_AST,    /* static analysis pass      */
    TASK_OPTIMISE,       /* peephole optimise chunk   */
    TASK_IDLE
} NeuralTaskKind;

typedef struct NeuralTask {
    NeuralTaskKind  kind;
    void           *data;         /* task-specific payload     */
    void          (*callback)(void *result, void *user_data);
    void           *user_data;
    int             done;
} NeuralTask;

typedef struct {
    NeuralTask  queue[NEURAL_QUEUE_SIZE];
    int         head;
    int         tail;
    int         count;
    int         core_id;
    int         busy;

    /* inter-core communication ring */
    int         next_core_id;
    int         prev_core_id;
} NeuralCore;

typedef struct {
    NeuralCore  cores[NEURAL_CORE_COUNT];
    int         round_robin;  /* next core to receive a task */
    size_t      tasks_dispatched;
    size_t      tasks_completed;
} NeuralNetwork;

NeuralNetwork *neural_create(void);
void           neural_destroy(NeuralNetwork *nn);

/* Submit a task — automatically routed to the least-busy core */
int  neural_submit(NeuralNetwork *nn, NeuralTaskKind kind,
                   void *data,
                   void (*callback)(void *result, void *user_data),
                   void *user_data);

/* Flush all pending tasks (synchronous drain) */
void neural_flush(NeuralNetwork *nn);

/* Stats */
void neural_stats(const NeuralNetwork *nn);

#endif /* MCBL_NEURAL_H */
