#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <threads.h>

typedef struct node {
    void* data;
    struct node* next;
} node;

typedef struct threadWaiting {
    cnd_t cond;
    struct threadWaiting* next;
} threadWaiting;

typedef struct Queue {
    node* head;
    node* tail;
    size_t itemCount;
    size_t waitingCount;
    size_t visitedCount;
    mtx_t lock;
    threadWaiting* waitingList;
} Queue;

Queue queue; //Global concurrent queue instance


void initQueue() {
    queue.head = NULL;
    queue.tail = NULL;
    queue.itemCount = 0;
    queue.waitingCount = 0;
    queue.visitedCount = 0;
    mtx_init(&queue.lock, mtx_plain);
    queue.waitingList = NULL;
}

void destroyQueue() {
    mtx_lock(&queue.lock);

    //Clean up the nodes in the queue
    node* curr_node = queue.head;
    while (curr_node != NULL) {
        node* next_node = curr_node->next;
        free(curr_node->data);
        free(curr_node);
        curr_node = next_node;
    }

    //Clean up the waiting threads
    threadWaiting* curr_thread = queue.waitingList;
    while (curr_thread != NULL) {
        threadWaiting* next_thread = curr_thread->next;
        cnd_destroy(&curr_thread->cond);
        free(curr_thread);
        curr_thread = next_thread;
    }

    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
}

//Function to add an item to the queue
void enqueue(void* item) {
    node* new_node = (node*)malloc(sizeof(node));
    new_node->data = item;
    new_node->next = NULL;

    mtx_lock(&queue.lock);

    //Insert the new node at the tail of the queue
    if (queue.tail == NULL) {
        queue.head = new_node;
        queue.tail = new_node;
    } else {
        queue.tail->next = new_node;
        queue.tail = new_node;
    }

    queue.itemCount++;
    queue.visitedCount++;

    //If there are sleeping threads, wake up the oldest sleeping one
    if (queue.waitingCount > 0) {
        threadWaiting* curr_thread = queue.waitingList;
        cnd_signal(&curr_thread->cond);
        queue.waitingList = curr_thread->next;
        free(curr_thread);
        queue.waitingCount--;
    }

    mtx_unlock(&queue.lock);
}

void* dequeue() {
    mtx_lock(&queue.lock);

    // Wait until there is an item in the queue
    while (queue.itemCount == 0) {
        //Create a new waiting thread object
        threadWaiting* curr_thread = (threadWaiting*)malloc(sizeof(threadWaiting));
        cnd_init(&curr_thread->cond);
        curr_thread->next = NULL;

        //Add the waiting thread to the list
        if (queue.waitingList == NULL) {
            queue.waitingList = curr_thread;
        } else {
            threadWaiting* last_thread = queue.waitingList;
            while (last_thread->next != NULL) {
                last_thread = last_thread->next;
            }
            last_thread->next = curr_thread;
        }

        queue.waitingCount++;

        cnd_wait(&curr_thread->cond, &queue.lock);

        cnd_destroy(&curr_thread->cond);
        free(curr_thread);
    }

    //Remove the node from the head of the queue
    node* curr_node = queue.head;
    void* item = curr_node->data;
    queue.head = curr_node->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }
    queue.itemCount--;

    mtx_unlock(&queue.lock);

    free(curr_node);

    return item;
}

bool tryDequeue(void** item) {
    mtx_lock(&queue.lock);

    if (queue.itemCount == 0) {
        mtx_unlock(&queue.lock);
        return false;
    }

    //Remove the node from the head of the queue
    node* curr_node = queue.head;
    *item = curr_node->data;
    queue.head = curr_node->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }
    queue.itemCount--;

    mtx_unlock(&queue.lock);

    free(curr_node);

    return true;
}

size_t size() {
    size_t itemCount = queue.itemCount;
    return itemCount;
}

size_t waiting() {
    mtx_lock(&queue.lock);
    size_t waitingCount = queue.waitingCount;
    mtx_unlock(&queue.lock);
    return waitingCount;
}

size_t visited() {
    size_t visitedCount = queue.visitedCount;
    return visitedCount;
}
