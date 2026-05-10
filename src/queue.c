#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* Ignore invalid enqueue requests and keep the queue bounded. */
        if (q == NULL || proc == NULL || q->size >= MAX_QUEUE_SIZE)
                return;

        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /*
         * Return the next "in turn" PCB from the queue.
         * In MLQ mode, sched.c decides which priority queue gets service;
         * dequeue() only removes the head element of that selected queue.
         */

        if (empty(q))
                return NULL;

        struct pcb_t *proc = q->proc[0];
        for (int i = 0; i < q->size - 1; i++)
                q->proc[i] = q->proc[i + 1];

        q->size--;
        q->proc[q->size] = NULL;

        return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (empty(q) || proc == NULL)
                return NULL;

        for (int i = 0; i < q->size; i++) {
                if (q->proc[i] == proc) {
                        struct pcb_t *removed = q->proc[i];
                        for (int j = i; j < q->size - 1; j++)
                                q->proc[j] = q->proc[j + 1];

                        q->size--;
                        q->proc[q->size] = NULL;
                        return removed;
                }
        }

        return NULL;
}
