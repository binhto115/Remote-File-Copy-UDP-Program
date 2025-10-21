// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

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
#include <math.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "functions.h"
#include "checksum.h"
#include "cpe464.h"
#include "pollLib.h"
#include "functions.h"

//#define MAXBUF 1400
#define MAX_RETRIES 10
#define TIMEOUT_MS 1000

// function instantiations 
int checkArgs(int argc, char * argv[]);
float getErrorRate(int argc, char *argv[]);
void processFile(int argc, char *argv[], int socketNum, struct sockaddr_in6 *server);



// -----State Machines-----
typedef enum State STATE;

enum State {
	START, DONE, WAIT_ON_FILE_OK, WAIT_ON_DATA, PROCESS_TRANSFER, SEND_EOF_ACK
};

// State Functions
STATE start_state(char *argv[], struct sockaddr_in6 *server, int socketNum, int portNumber);
STATE wait_on_file_ok_state(char *argv[], struct sockaddr_in6 *server, int socketNum, int portNumber);
STATE wait_on_data_state(char *argv[], struct sockaddr_in6 *recvAddr, int socketNum);
STATE process_transfer_state(ReceiveInfo *info);
STATE send_eof_ack_state(ReceiveInfo *info, uint32_t eofSequence);

// -----Main----- 
int main (int argc, char *argv[]) {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = checkArgs(argc, argv);
	float errorRate = atof(argv[5]);
		
	// Grab socket number
	socketNum = setupUdpClientToServer(&server, argv[6], portNumber);

	// Initialize sendErr_init Library
	sendErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);

	// The start of the state transition	
	processFile(argc, argv, socketNum, &server);
		
	return 0;
}

void processFile(int argc, char *argv[], int socketNum, struct sockaddr_in6 *server) {		
	// Grab port-num
	int portNumber = checkArgs(argc, argv); //7
	
	// Grab socket number
	socketNum = setupUdpClientToServer(server, argv[6], portNumber);

	// Initialize ReceiveInfo
	ReceiveInfo info;
	info.socketNum = socketNum;
	info.serverAddr = *server;
	info.serverLen = sizeof(info.serverAddr);
	
	// ----- State Loop -----
	STATE state = START;
	struct sockaddr_in6 recvAddr;
	while (state != DONE) {
		switch (state) {
			case START:
				state = start_state(argv, server, socketNum, portNumber);
				break;
			case WAIT_ON_FILE_OK:
				state = wait_on_file_ok_state(argv, server, socketNum, portNumber);
				break;
			case WAIT_ON_DATA:
				state = wait_on_data_state(argv, &recvAddr, socketNum);
				break;
			case PROCESS_TRANSFER:
				state = process_transfer_state(&info);					
				break;
			case SEND_EOF_ACK:
				state = send_eof_ack_state(&info, info.eofSeq);
				break;
			case DONE:
				printf("DONE.\n");
				break;
			default:
				printf("ERROR: in default state!\n");
				break;
		}

	}
}

// ----- Start State -> Wait on File Ok State -----
STATE start_state(char *argv[], struct sockaddr_in6 *server, int socketNum, int portNumber) {
	// Initialize variables
	STATE returnValue = WAIT_ON_FILE_OK;

	// Create UDP Client to Server
	socketNum = setupUdpClientToServer(server, argv[6], portNumber);
	if (socketNum < 0) {
		returnValue = DONE;
	}
	
	//close(socketNum);
	return returnValue;
}

// ----- Wait on File Ok State -> Wait on Data -----
STATE wait_on_file_ok_state(char *argv[], struct sockaddr_in6 *server, int socketNum, int portNumber) {
	// -----Initialize variables-----
	int count = 0;
	STATE returnValue = DONE; // WAIT_ON_FILE_OK
	
	// To send
	uint8_t payload[MAXBUF];
	//uint8_t *payload = (uint8_t *)argv[1]; // from-filename name
	uint8_t *fromFilename = (uint8_t *)argv[1]; // from-filename
	uint16_t windowSize = htons(atoi(argv[3])); // Window Size
	uint16_t bufferSize = htons(atoi(argv[4])); // Buffer Size
	
	int fileNameLen = strlen(argv[1]); // length of from-filename
	int serverAddrLen = sizeof(struct sockaddr_in6);;

	// Copy into payload	
	memcpy(payload, &windowSize, 2);
	memcpy(payload + 2, &bufferSize, 2);
	memcpy(payload + 4, fromFilename, fileNameLen);
		
	//printf("Sending:\n  windowSize: %d\n  bufferSize: %d\n  filename: %s\n",
       	//	ntohs(windowSize), ntohs(bufferSize), fromFilename);

	// Create PDU
	int pduLen = 0;
	uint8_t pdu[MAXBUF+7];
	uint32_t sequenceNum = 0;
	uint8_t flag = 8;
	pduLen = createPDU(pdu, sequenceNum, flag, payload, fileNameLen + 4);

	while (count < MAX_RETRIES) {
		// Close and open a new socket
		if (socketNum > 0) {
			close(socketNum);
		}
		
		socketNum = setupUdpClientToServer(server, argv[6], portNumber);
		if (socketNum < 0) {
			printf("ERROR: Socket failed.\n");
			return DONE;
		}

		// -----Start Polling------
		setupPollSet();		
		addToPollSet(socketNum);
		//sendErr_init(atof(argv[5]), DROP_OFF, FLIP_OFF, DEBUG_ON, RSEED_OFF);
		sendErr_init(atof(argv[5]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);

		// send PDU
		sendtoErr(socketNum, pdu, pduLen, 0, (struct sockaddr *)server, serverAddrLen);
	//	printf("[Client %d] attempted %d: Sent filename: %s\n", socketNum, count+1,  argv[1]);
		
		// Start timer
		int socketReady = pollCall(TIMEOUT_MS); // 1-second poll
		if (socketReady == -1) {
			printf("WARNING: Timeout waiting for server to respond!\n");
			count++;
			close(socketNum);
			continue;
		}

		// Receive file ok ack
	 	uint8_t recvBuff[MAXBUF+7];
		struct sockaddr_in6 recvAddr;	
		int recvLen = sizeof(recvAddr);
		int recvBytes = safeRecvfrom(socketNum, recvBuff, sizeof(recvBuff), 0, (struct sockaddr *)&recvAddr, &recvLen);
		if (recvBytes < 0) {
			printf("ERROR: recvfrom() failed.\n");
			count++;
			close(socketNum);
			continue;
		}

		// Check for filename OK
		uint8_t recvFlag = recvBuff[6];
		if (recvFlag == 9) {	
			// -----Attempt to Open Output File-----
		        char *toFileName = argv[2];			
			FILE *OutputFile = fopen(toFileName, "wb");
			if (OutputFile == NULL) {
				printf("Error on open of output file: %s\n", toFileName);
				close(socketNum);
				return DONE;	
			}	
		
			// -----Send FILE OK ACK (flag = 34)-----
			uint8_t file_ok_ack_pdu[MAXBUF];
			int file_ok_ack_len = createPDU(file_ok_ack_pdu, sequenceNum, 34, NULL, 0);
			sendtoErr(socketNum, file_ok_ack_pdu, file_ok_ack_len, 0, (struct sockaddr *)&recvAddr, recvLen);
	//		printf("[Client %d] sent FILE OK ACK (flag 34).\n", socketNum);
	
			return WAIT_ON_DATA;

			close(socketNum);
			return DONE;
		}else {
			count++;
			close(socketNum);
			return WAIT_ON_FILE_OK;
		}

	}	
		
	// All retries have been attempted	
	return returnValue;	
}

STATE wait_on_data_state(char *argv[], struct sockaddr_in6 *recvAddr, int socketNum) {
	// Set Receiver Info
	ReceiveInfo info;
	info.windowSize = atoi(argv[3]);
	info.expected = 1;
	info.highest = 0;
	info.socketNum = socketNum;
	info.buffer = calloc(info.windowSize, sizeof(PacketEntry));

	if (!info.buffer) {
		printf("ERROR: Unable to allocate packet buffer.\n");
		return DONE;
	}

	// Open the output file 
	info.outFile = fopen(argv[2], "wb");
	if (!info.outFile) {
		printf("ERROR: Unable to open the output file: %s\n", argv[2]);
		free(info.buffer);	
		return DONE;
	}

	// Copy the sender address from previous response
	memcpy(&info.serverAddr, recvAddr, sizeof(struct sockaddr_in6));
	info.serverLen = sizeof(struct sockaddr_in6);

	// File reception state machine
	STATE nextState = process_transfer_state(&info);

	return nextState; // DONE after receiving the whole file
}

// -----PROCESS TRANSFER STATE-----
STATE process_transfer_state(ReceiveInfo *info) {
	TRANSFER_STATE state = IN_ORDER;
	info->serverLen = sizeof(info->serverAddr);

	// -----Start the Mini State Machine-----
	while (1) {
		uint8_t packet[MAXBUF + 7];
		int bytesRecv = safeRecvfrom(info->socketNum, packet, sizeof(packet), 0, (struct sockaddr *)&(info->serverAddr), (int *)(&info->serverLen));
		if (bytesRecv < 0) {
			continue;	
		}

		// Extract info
		uint32_t seqNum;
		memcpy(&seqNum, packet, 4);
		seqNum = ntohl(seqNum);
	
		uint8_t flag = packet[6];
		int payloadLen = bytesRecv - 7;
		uint8_t *payload = packet + 7;

		// Debugging - if this never prints, the client isn't getting packets
		// Most likely the server is sending to wrong port or packets are flipped/dropped
		//printf("[Client %d] RECV seq=%u, flag=%u, len=%d\n", info->socketNum, seqNum, flag, payloadLen);

		// Handle EOF 
		if (flag == 10) {
			printf("[Client] received EOF (flag 10) seq #%u.\n", seqNum);

			info->eofSeq = seqNum;
			
			// Flush the rest
		        while (info->expected <= info->highest && info->buffer[info->expected % info->windowSize].valid) {
		                PacketEntry *entry = &info->buffer[info->expected % info->windowSize];
                		fwrite(entry->packet, 1, entry->packetLen, info->outFile);
	                	entry->valid = 0;
        	        	info->expected++;
            		}


		        return send_eof_ack_state(info, seqNum);
		}

		// Data Packet (flags 16/17/18)
		if (flag == 16 || flag == 17 || flag == 18) {
			switch (state) {
				case IN_ORDER:
					if (seqNum == info->expected) {
						fwrite(payload, 1, payloadLen, info->outFile);
						info->expected++;
						info->highest = seqNum;
						send_rr(info, info->expected);	
					} else if (seqNum > info->expected) {
						buffer_packet(info, seqNum, payload, payloadLen);
						if (seqNum > info->highest) {
							info->highest = seqNum;
						}
						send_srej(info, info->expected);
						state = OUT_OF_ORDER;
						/*info->highest = seqNum;
						send_srej(info, info->expected);
						state = OUT_OF_ORDER;	*/
					}
					break;
				case OUT_OF_ORDER:
					if (seqNum == info->expected) {
						fwrite(payload, 1, payloadLen, info->outFile);
						info->expected++;
						send_rr(info, info->expected);	
						state = FLUSH;
					} else if (seqNum > info->expected) {
						buffer_packet(info, seqNum, payload, payloadLen);
						if (seqNum > info->highest) {
							info->highest = seqNum;
						}
					}
					break;
				case FLUSH:
					while ((info->expected <= info->highest) && (info->buffer[info->expected % info->windowSize].valid)) {
						PacketEntry * entry = &info->buffer[info->expected % info->windowSize];
						fwrite(entry->packet, 1, entry->packetLen, info->outFile);
						entry->valid = 0;
						info->expected++;
					}
				
					if (info->expected <= info->highest) {
						PacketEntry *entry = &info->buffer[info->expected % info->windowSize];
						if (!entry->valid) {
							send_srej(info, info->expected);
							state = OUT_OF_ORDER;
						}
					} else {
						send_rr(info, info->expected);
						state = IN_ORDER;
					}
					break;					
			}
		}
	}
	return DONE;
}

// -----SEND EOF ACK STATE-----
STATE send_eof_ack_state(ReceiveInfo *info, uint32_t eofSequence) {
	uint8_t ackPDU[7];
	int ackLen = createPDU(ackPDU, eofSequence, 35, NULL, 0);
	
	sendtoErr(info->socketNum, ackPDU, ackLen, 0, (struct sockaddr *)&(info->serverAddr), info->serverLen);
	
	//printf("[Client] sent EOF ACK (flag 35) for seq #%u\n", eofSequence);

	// Flush
	fflush(info->outFile);
	fclose(info->outFile);
	close(info->socketNum);

	return DONE;
}

// -----Check rcopy Command-line Argument-----
int checkArgs(int argc, char * argv[]) {
	// Initialize variables
        int portNumber = 0;
	float errorRate = 0.0;
	
        /* check command line arguments  */
	if (argc != 8) {
		printf("Usage: %s from-filename to-filename window-size buffer-size error-rate host-name port-number \n", argv[0]);
		exit(1);
	}

	// Check to-filename length 
	if (strlen(argv[1]) > 100) {
		printf("ERROR: Filename exceeds 100 characters!\n");
		exit(-1);
	}	

	// Check Window Size input
	if (atoi(argv[3]) <= 0 || atoi(argv[3]) >= pow(2, 30)) {
		printf("ERROR: Invalid Window Size!\n");
		exit(-1);
	}

	// Check Error Rate input
	errorRate = atof(argv[5]);
	if (errorRate < 0 || errorRate >= 1) {
		printf("ERROR: Invalid Error Rate!\n");
		exit(-1);
	}			
	
	portNumber = atoi(argv[7]);		
	return portNumber;
}





