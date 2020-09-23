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
    pthread_mutex_t write_lock;
    pthread_mutex_t read_lock;
    sem_t empty;
    sem_t full;

    int write_index;
    int read_index;
    int size;

    void **actions;
} Queue;


/**
 * Allocate a concurrent queue of a specific size
 * @param size - The size of memory to allocate to the queue
 * @return queue - Pointer to the allocated queue
 */
Queue *queue_alloc(int size) {
    Queue *queue = malloc(sizeof(Queue));
    queue->actions = malloc(sizeof(void *) * size);

    pthread_mutex_init(&queue->write_lock, NULL);
    pthread_mutex_init(&queue->read_lock, NULL);

    sem_init(&queue->empty, 0, 0);
    sem_init(&queue->full, 0, size);

    queue->write_index = 0;
    queue->read_index = 0;
    queue->size = size;

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
    pthread_mutex_lock(&queue->write_lock);
    sem_wait(&queue->full);

    queue->actions[queue->write_index] = item;
    queue->write_index = (queue->write_index + 1) % queue->size;

    sem_post(&queue->empty);
    pthread_mutex_unlock(&queue->write_lock);
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

    pthread_mutex_lock(&queue->read_lock);
    sem_wait(&queue->empty);

    action = queue->actions[queue->read_index];
    queue->actions[queue->read_index] = NULL;
    queue->read_index = (queue->read_index + 1) % queue->size;
    
    sem_post(&queue->full);
    pthread_mutex_unlock(&queue->read_lock);

    return action;
}

