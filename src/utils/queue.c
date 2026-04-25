#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "queue.h"

static void ensure_capacity(Queue *q) {
    if (q->size == q->capacity) {
        int new_capacity = q->capacity * 2;
        void **new_items = (void **)malloc(sizeof(void *) * new_capacity);
        for (int i = 0; i < q->size; i++) {
            new_items[i] = q->items[(q->front + i) % q->capacity];
        }
        free(q->items);
        q->items = new_items;
        q->capacity = new_capacity;
        q->front = 0;
        q->rear = q->size;
    }
}

Queue* create_queue(int capacity){
    Queue *q = (Queue *)malloc(sizeof(Queue));
    q->capacity = capacity;
    q->front = 0;
    q->rear = 0;
    q->size = 0;
    q->items = (void **)malloc(sizeof(void *) * capacity);
    return q;
}
Queue* queue_create(int capacity){
    return create_queue(capacity);
}

void queue_destroy(Queue *q){
    if(q){
        if(q->items) 
         free(q->items);
        free(q);
    }
}
int queue_is_empty(Queue *q){
    return q->size == 0;
}
int queue_enqueue(Queue *q, void *item){
    ensure_capacity(q);
    q->items[q->rear] = item;
    q->rear = (q->rear + 1) % q->capacity;
    q->size++;
    return 0;
}

void* queue_dequeue(Queue *q) {
    if(queue_is_empty(q)) return NULL;
    void *item = q->items[q->front];
    q->front = (q->front + 1) % q->capacity;
    q->size--;
    return item;
}
