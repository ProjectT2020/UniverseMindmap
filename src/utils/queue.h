
#ifndef UM_QUEUE_H
#define UM_QUEUE_H

typedef struct {
    void **items;
    int capacity;
    int front;
    int rear;
    int size;
} Queue;

Queue* create_queue(int capacity);
Queue* queue_create(int capacity);
void queue_destroy(Queue *q);
int queue_is_empty(Queue *q);
int queue_enqueue(Queue *q, void *item);
void* queue_dequeue(Queue *q);

#endif // UM_QUEUE_H