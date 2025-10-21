#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdint.h>
#include "networks.h"
#include "safeUtil.h"
#include "functions.h"
#include "checksum.h"
#include "cpe464.h"


int createPDU(uint8_t *pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int payloadLen) {
	// create PDU code goes here
	int pduLength = 0;
	int offset = 0;

	// Copy Sequence number into buffer
	uint32_t sequenceNumberNetwork = htonl(sequenceNumber);
	memcpy(pduBuffer, &sequenceNumberNetwork, sizeof(sequenceNumberNetwork));
	offset+= sizeof(sequenceNumberNetwork);
 	
	// Calculate and copy Checksum into buffer
	uint16_t ck_sum = 0;
	memcpy(pduBuffer + offset, &ck_sum, sizeof(ck_sum));
	offset+= 2;
	
	// Copy flag into buffer
	memcpy(pduBuffer + offset, &flag, sizeof(flag));
	offset+= sizeof(flag);
	
	// Copy payload into buffer
	memcpy(pduBuffer + offset, payload, payloadLen);
	offset+= payloadLen;
	
	// Calculate checksum on the entire PDU with ck_sum set to 0
	ck_sum = in_cksum((unsigned short *)pduBuffer, offset);
	//uint16_t ck_sum_network = htons(ck_sum);
	memcpy(pduBuffer + 4, &ck_sum, sizeof(ck_sum));

	// Return PDU length	
	pduLength = offset;
	return pduLength;

}

void printPDU(uint8_t *aPDU, int pduLength) {
	// -----Extract fields-----
	
	// Check checksum
	if (in_cksum((unsigned short*)aPDU, pduLength) != 0) {
		printf("Invalid checksum: PDU is corrupted.\n");
		exit(-1);
	}

		
	// Extract 4 bytes of sequence number
	int offset = 0;
	uint32_t sequenceNumber;
	memcpy(&sequenceNumber, aPDU, 4);
	sequenceNumber = ntohl(sequenceNumber);
	offset+= sizeof(sequenceNumber);
	
	// Extract two bytes
	uint16_t checksum;
	memcpy(&checksum, aPDU + offset, 2);
	offset+= sizeof(checksum);

	uint8_t flag;
	memcpy(&flag, aPDU + offset, 1);
	offset+= sizeof(flag);

	uint8_t *payload = aPDU + offset;
	int payloadLen = pduLength - offset;
   	
	// Print it
	printf("\n");
	printf("Printing Payload Content:\n");
	printf("-------------------------------------------\n");
	printf("Sequence number: %u\n", sequenceNumber);
	printf("Flag: %u\n", flag);
	printf("Payload: %.*s\n", payloadLen, payload);
	printf("Payload Length: %d\n", payloadLen);
	printf("-------------------------------------------\n");
}

void send_rr(ReceiveInfo *info, uint32_t next) {
	uint8_t pdu[7];
	uint32_t totalSeq = htonl(next);
	memcpy(pdu, &totalSeq, 4);
	memset(pdu + 4, 0, 2);
	pdu[6] = 5;

	uint16_t checksum = in_cksum((unsigned short *)pdu, sizeof(pdu));
	memcpy(pdu + 4, &checksum, 2);

	//pdu[4] = 0;
	//pdu[5] = 0;
	//pdu[6] = 5; // RR
	sendtoErr(info->socketNum, pdu, sizeof(pdu), 0, (struct sockaddr *)&info->serverAddr, info->serverLen);
//	printf("Sent RR %u\n", next);

}

void send_srej(ReceiveInfo *info, uint32_t missingSeq) {
   	uint8_t pdu[7];
	uint32_t netSeq = htonl(missingSeq);
	memcpy(pdu, &netSeq, 4);
	
	memset(pdu + 4, 0, 2);  // Zero the checksum field first
    	pdu[6] = 6; // SREJ flag

   	uint16_t checksum = in_cksum((unsigned short *)pdu, sizeof(pdu));
	memcpy(pdu + 4, &checksum, 2);

	sendtoErr(info->socketNum, pdu, sizeof(pdu), 0, (struct sockaddr *)&info->serverAddr, info->serverLen);
/*	pdu[4] = 0; 
	pdu[5] = 0;
	pdu[6] = 6; // SREJ
	sendtoErr(info->socketNum, pdu, 7, 0, (struct sockaddr *)&info->serverAddr, info->serverLen);
//	printf("Sent SREJ %u\n", missingSeq);
*/
}


void buffer_packet(ReceiveInfo *info, uint32_t seq, uint8_t *data, int len) {
    int index = seq % info->windowSize;
    PacketEntry *entry = &info->buffer[index];

    if (!entry->valid) {
        memcpy(entry->packet, data, len);
        entry->packetLen = len;
        entry->valid = 1;
 //       printf("Buffered seq#%u\n", seq);
    }
}












