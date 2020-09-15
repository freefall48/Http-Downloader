#include "queue.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)



/*
 * Queue - the abstract type of a concurrent queue.
 * You must provide an implementation of this type 
 * but it is hidden from the outside.
 */
typedef struct QueueStruct {
    pthread_mutex_t lock;
    sem_t readable;
    sem_t writable;

    void **actions;

    int next_pos;
    int size;
} Queue;


/**
 * Allocate a concurrent queue of a specific size
 * @param size - The size of memory to allocate to the queue
 * @return queue - Pointer to the allocated queue
 */
Queue *queue_alloc(int size) {
    Queue *queue = (Queue *) malloc(sizeof(Queue));

    queue->actions = malloc(sizeof(void *) * size);
    queue->next_pos = 0;
    queue->size = size;

    pthread_mutex_init(&queue->lock, NULL);
    sem_init(&queue->readable, 0, 0);
    sem_init(&queue->writable, 0, size);

    return queue;
}


/**
 * Free a concurrent queue and associated memory 
 *
 * Don't call this function while the queue is still in use.
 * (Note, this is a pre-condition to the function and does not need
 * to be checked)
 * 
 * @param queue - Pointer to the queue to free
 */
void queue_free(Queue *queue) {
    free(queue->actions);
    free(queue);
}


/**
 * Place an item into the concurrent queue.
 * If no space available then queue will block
 * until a space is available when it will
 * put the item into the queue and immediatly return
 *  
 * @param queue - Pointer to the queue to add an item to
 * @param item - An item to add to queue. Uses void* to hold an arbitrary
 *               type. User's responsibility to manage memory and ensure
 *               it is correctly typed.
 */
void queue_put(Queue *queue, void *item) {
    // Check if there is a position to put the item.
    sem_wait(&queue->writable);
    pthread_mutex_lock(&queue->lock);

    queue->actions[queue->next_pos] = item;
    queue->next_pos++;

    pthread_mutex_unlock(&queue->lock);
    sem_post(&queue->readable);
    }


/**
 * Get an item from the concurrent queue
 * 
 * If there is no item available then queue_get
 * will block until an item becomes avaible when
 * it will immediately return that item.
 * 
 * @param queue - Pointer to queue to get item from
 * @return item - item retrieved from queue. void* type since it can be 
 *                arbitrary 
 */
void *queue_get(Queue *queue) {
    void *action;

    sem_wait(&queue->readable);
    pthread_mutex_lock(&queue->lock);

    action = queue->actions[queue->next_pos - 1];
    queue->next_pos--;

    pthread_mutex_unlock(&queue->lock);
    sem_post(&queue->writable);
    

    return action;
}

