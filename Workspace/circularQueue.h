#ifndef  CIRCULAR_QUEUE_H
#define CIRCULAR_QUEUE_H

#include <stdint.h>

typedef struct {
	uint8_t *packet;
	int packetLen;
	uint32_t sequenceNum;
	int valid;
} QueueEntry;

typedef struct {
	QueueEntry *entries; // Dynamically allocated array
	int WindowSize;
	int ValidCount;
} CircularQueue;

int CircularQueue_init(CircularQueue *queue, int windowSize);
int CircularQueue_insert(CircularQueue *queue, uint32_t sequenceNum, uint8_t *packet, int packetLen);
QueueEntry *CircularQueue_get(CircularQueue *queue, uint32_t sequenceNum);
int CircularQueue_remove(CircularQueue *queue, uint32_t sequenceNum);
int CircularQueue_is_full(CircularQueue *queue);
int CircularQueue_is_empty(CircularQueue *queue);
int CircularQueue_clear(CircularQueue *queue);
int CircularQueue_free(CircularQueue *queue);


#endif
