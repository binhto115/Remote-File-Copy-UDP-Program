#ifndef __FUNCTION_H__
#define __FUNCTION_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define MAXBUF 1400

// Process Transfer Struct
typedef enum {
	IN_ORDER, OUT_OF_ORDER, FLUSH
} TRANSFER_STATE;

typedef struct {
    uint8_t packet[MAXBUF];
    int packetLen;
    int valid;
} PacketEntry;

typedef struct {
	PacketEntry *buffer;
	int windowSize;
	uint32_t expected;
	uint32_t highest;
	FILE *outFile;
	int socketNum;
	struct sockaddr_in6 serverAddr;
	socklen_t serverLen;
	uint32_t eofSeq;
} ReceiveInfo;


// pdubuffer is the buffer you fill with your PDU header/payload
// sequencenumber = 32 bit sequence number passed in in host order
// flag = the type of PDU (e.g flag = 3 means data)
// payload = payload (data) of the PDU (treat the payload as just bytes)
// dataLen = length of the payload (so # of bytes in data),
// 	     this is used to memcpy the data into the PDU
// returns the length of the created PDU
int createPDU(uint8_t *pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int payloadLen);

void printPDU(uint8_t *aPDU, int pduLength);

void send_rr(ReceiveInfo *info, uint32_t next);

void send_srej(ReceiveInfo *info, uint32_t missingSeg);

void buffer_packet(ReceiveInfo *info, uint32_t seq, uint8_t *data, int len);
#endif 
