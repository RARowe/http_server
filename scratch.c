char file[512];
size_t file_size;

typedef struct {
    pthread_mutex_t mutex;
    int front, back, size;
    int buffer[128];
} queue_t;

void init_queue(queue_t* q) {
    q->front = 0;
    q->back = 0;
    q->size = 0;
}

void enqueue(queue_t* q, int sock) {
    pthread_mutex_lock(&q->mutex);

    q->buffer[q->back] = sock;
    q->back++;
    if (q->back > 127) {
        q->back = 0;
    }
    q->size++;

    pthread_mutex_unlock(&q->mutex);
}

int dequeue(queue_t* q) {
    pthread_mutex_lock(&q->mutex);

    int val = q->buffer[q->front];

    q->front++;
    if (q->front > 127) {
        q->front = 0;
    }
    q->size--;

    pthread_mutex_unlock(&q->mutex);
    return val;
}
