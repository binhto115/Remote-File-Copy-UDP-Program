// ----- Circular Queue Library -----

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "circularQueue.h"

int CircularQueue_init(CircularQueue *queue, int windowSize) {
	queue->entries = (QueueEntry *)calloc(windowSize, sizeof(QueueEntry));
	if (queue->entries == NULL) {
		return -1;
	}
	
	queue->WindowSize = windowSize;
	queue->ValidCount = 0;
	
	for (int i = 0; i < windowSize; i++) {
		queue->entries[i].valid = 0;
		queue->entries[i].packet = NULL;
	}
	return 0;
}



int CircularQueue_insert(CircularQueue *queue, uint32_t sequenceNum, uint8_t *packet, int packetLen) {
	if (CircularQueue_is_full(queue)) {
        	return -1;
    	}
    		int index = sequenceNum % queue->WindowSize;
    	if (queue->entries[index].valid) {
        	return -1;
    	}
    	queue->entries[index].packet = malloc(packetLen);
    	if (!queue->entries[index].packet) {
		return -1;
	
	}

	memcpy(queue->entries[index].packet, packet, packetLen);
    	queue->entries[index].packetLen = packetLen;
    	queue->entries[index].sequenceNum = sequenceNum;
    	queue->entries[index].valid = 1;
 	queue->ValidCount++;
    	return 0;
}

QueueEntry *CircularQueue_get(CircularQueue *queue, uint32_t sequenceNum) {
	int index = sequenceNum % queue->WindowSize;
	// before: !queue->entries[index].valid
	if (queue->entries[index].valid || queue->entries[index].sequenceNum != sequenceNum) {
		return &queue->entries[index];
	}
	return NULL;
}

int CircularQueue_remove(CircularQueue *queue, uint32_t sequenceNum) {
	int index = sequenceNum % queue->WindowSize;
	if (!queue->entries[index].valid || queue->entries[index].sequenceNum != sequenceNum) {
		return -1;
	}
	free(queue->entries[index].packet);
	queue->entries[index].packet = NULL;
	queue->entries[index].valid = 0;
	queue->ValidCount--;
	return 0;
	
}

int CircularQueue_is_full(CircularQueue *queue) {
	return queue->ValidCount >= queue->WindowSize;
}

int CircularQueue_is_empty(CircularQueue *queue) {
	return queue->ValidCount == 0;
}

int CircularQueue_clear(CircularQueue *queue) {
	for (int i = 0; i < queue->WindowSize; i++) {
		if (queue->entries[i].valid) {
			free(queue->entries[i].packet);
			queue->entries[i].packet = NULL;
			queue->entries[i].valid = 0;
		}
	}
	queue->ValidCount = 0;
	return 0;
}

int CircularQueue_free(CircularQueue *queue) {
	CircularQueue_clear(queue);
	free(queue->entries);
	queue->entries = NULL;
	queue->WindowSize = 0;
	return 0;
}




