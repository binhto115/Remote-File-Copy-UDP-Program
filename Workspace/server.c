/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>

#include "pollLib.h"
#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "functions.h"
#include "checksum.h"
#include "cpe464.h"
#include "circularQueue.h"


typedef enum State STATE;
enum State {
	START, FILENAME, WRITE_FILE_OK_ACK, SEND_DATA, WAIT_ON_ACK, WAIT_ON_EOF_ACK, RESEND_EOF, DONE
};


// ----- Function Prototypes -----
void processServer(char *argv[], int socketNum);
void processClient(char *argv[], struct sockaddr_in6 clientAddr, int socketNum, uint8_t *buffer, int bytesRecv);
int checkArgs(int argc, char *argv[]);
float getErrorRate(int argc, char *argv[]);



// ----- ServerInfo Struct-----
typedef struct {
	int childSocket;
	FILE *file;
	struct sockaddr_in6 clientAddr;
	uint16_t windowSize;
	uint16_t bufferSize;
	int eofLen;
	uint8_t eofPacket[MAXBUF+7];
	uint32_t eofSeq;
	int eofResendCount;
} ServerInfo;

// ----- STATE MACHINE ----
STATE filename_state(char *argv[], int socketNum, uint8_t *buffer, int bytesRecv, ServerInfo *info);
STATE write_file_ok_ack_state(ServerInfo *info);
STATE send_data_state(CircularQueue *window, ServerInfo *info);
STATE wait_on_ack_state(CircularQueue *window, ServerInfo *info);
STATE wait_on_eof_ack_state(CircularQueue *window, ServerInfo *info);
STATE resend_eof_state(CircularQueue *window, ServerInfo *info);

void handleZombies(int signal) {
	while (waitpid(-1, NULL, WNOHANG) > 0);
}


// ===== Main =====
int main (int argc, char *argv[]) { 
	int mainSocketNum = 0;				
	int portNumber = 0;
	float errorRate = 0.0;
	
	// Grab a port number and a socket number
	portNumber = checkArgs(argc, argv);
	mainSocketNum = udpServerSetup(portNumber);

	// Initialize sendError
	errorRate = getErrorRate(argc, argv);
	sendErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);
	//sendErr_init(errorRate, DROP_OFF, FLIP_OFF, DEBUG_ON, RSEED_OFF);

	// Where everything starts 
	processServer(argv, mainSocketNum);
	
	// Close socketi
	close(mainSocketNum);
	
	return 0;
}

void processServer(char *argv[], int socketNum) {
	pid_t pid = 0;
	uint8_t buffer[MAXBUF];
	uint8_t flag = 0;
	//uint32_t recvLen = 0;

	// Receive filanem
	struct sockaddr_in6 clientAddr;
	int clientLen = sizeof(clientAddr);
	int bytesRecv;
	
	// Clean up before forking
	signal(SIGCHLD, handleZombies);

	// Get a new client, fork() a child
	while (1) {
		bytesRecv = safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) &clientAddr, &clientLen);
		printf("received filename.\n");		
		// Check if it receives any bytes
		if (bytesRecv < 0) {
			printf("ERROR: recvfromErr failed.\n");
			continue;
		}

		// Check Flag
		flag = buffer[6];
		if (flag == 8) {
			pid = fork();
			if (pid < 0) {
				printf("ERROR: pid failed.\n");
				exit(-1);
			} else if (pid == 0) {
				// ----- Child -----
				processClient(argv, clientAddr, socketNum, buffer, bytesRecv);
			} else {
				// ----- Parent -----
				continue;
			}
		} 
	}
}


void processClient(char *argv[], struct sockaddr_in6 clientAddr, int socketNum, uint8_t *buffer, int bytesRecv) {
	STATE state = START; // State Transition
 
	// -----Setup Struct-----
	ServerInfo info = {0};
	info.clientAddr = clientAddr;

	// -----Initialize CircularQueue-----
	CircularQueue window;
	//CircularQueue_init(&window, info.windowSize);

	while (state != DONE) {
		switch (state) {
			case START:
				state = FILENAME;
				break;
			case FILENAME:
				state = filename_state(argv, socketNum, buffer, bytesRecv, &info);
				break;
			case WRITE_FILE_OK_ACK:
				state = write_file_ok_ack_state(&info); 
				break;
			case SEND_DATA:
				CircularQueue_init(&window, info.windowSize);
				state = send_data_state(&window, &info);
				break;
			case WAIT_ON_ACK:
				state = wait_on_ack_state(&window, &info);
				break;
			case WAIT_ON_EOF_ACK:
				state = wait_on_eof_ack_state(&window, &info);
				break;
			case RESEND_EOF:
				state = resend_eof_state(&window, &info);
				break;
			case DONE:
				printf("DONE.\n");
				return;
			default:
				printf("ERROR: You should not be here!\n");
				state = DONE;
				break;
		}
	}
	
	if (info.windowSize > 0) {
		CircularQueue_free(&window);
	}

	//printf("[Server] EOF ACK received and child exiting cleanly.\n");
	// clean up
	/*if (info.file) {
		fclose(info.file);
	}
	
	if (info.childSocket != -1) {
		close(info.childSocket);
	}*/
	fclose(info.file);
	close(info.childSocket); // 1st change before //close(info.childSocket);
	
}

// -----FILENAME STATE-----
STATE filename_state(char *argv[], int socketNum, uint8_t *buffer, int bytesRecv, ServerInfo *info) {
	STATE returnValue = DONE;

	// ----- Child -----
	close(socketNum); // close main socket
			
	// Initialize sendErr_init
	sendErr_init(atof(argv[1]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);	
	//sendErr_init(atof(argv[1]), DROP_OFF, FLIP_OFF, DEBUG_ON, RSEED_OFF);	

	info->childSocket = udpServerSetup(0);// socket(AF_INET6, SOCK_DGRAM, 0);
	if (info->childSocket < 0) {
		printf("ERROR: Child Socket failed.\n");
		exit(-1);
	}	

	// Passing the child socket for the specific client
	//*childSocketOut = childSocket;
	socklen_t clientLen = sizeof(info->clientAddr);
	
	// Extract payload
	uint16_t windowSize;
	uint16_t bufferSize;
	memcpy(&windowSize, buffer + 7, 2);
	memcpy(&bufferSize, buffer + 9, 2);
	info->windowSize = ntohs(windowSize);
	info->bufferSize = ntohs(bufferSize);

	// Extract filename
	int filenameLen = bytesRecv - 11; // bytes after the header
	char filename[100]; // filename length of 100
	memcpy(filename, buffer + 11, filenameLen);
	filename[filenameLen] = '\0';
	
	//printf("Received request:\n  Window Size: %d\n  Buffer Size: %d\n  Filename: %s\n", 
	//	info->windowSize, info->bufferSize, filename);



	// Opening File
	FILE *file = fopen(filename, "rb");	
	if (file == NULL) {
		// Send Error flag 33
		uint8_t errorPDU[MAXBUF];
		int errorLen  = createPDU(errorPDU, 0, 33, (uint8_t *)filename, strlen(filename));
		sendtoErr(info->childSocket, errorPDU, errorLen, 0, (struct sockaddr *)&(info->clientAddr), clientLen);
		printf("filename: %s can't be open! sending file error 33 ack.\n", filename);
		return DONE;
	} else {
		// Send OK flag 9
		uint8_t okPDU[MAXBUF];
		int okLen = createPDU(okPDU, 0, 9, (uint8_t *)filename, strlen(filename));
		sendtoErr(info->childSocket, okPDU, okLen, 0, (struct sockaddr *)&(info->clientAddr), clientLen);	
		//printf("[Server] filename: %s can be open. Sending Filenam OK ACK (flag 9).\n", filename);
		returnValue = WRITE_FILE_OK_ACK;
	}
	
	// Updating Server information
	info->file = file; // Passing file pointer back to processClient
	info->windowSize = windowSize;
	info->bufferSize = bufferSize;
	return returnValue;
}

// -----WRITE FILE OK ACK STATE-----
STATE write_file_ok_ack_state(ServerInfo *info) {
	STATE returnValue = DONE;
	uint8_t buffer[MAXBUF];
	socklen_t clientLen = sizeof(info->clientAddr);
	int count = 0;

	setupPollSet();
	addToPollSet(info->childSocket);

	while (count < 10) {
		int socketReady = pollCall(1000);
		if (socketReady != -1) {
			int bytesRecv = safeRecvfrom(info->childSocket, buffer, MAXBUF, 0, (struct sockaddr *)&(info->clientAddr), (int *) &clientLen);
			if (bytesRecv < 0) {
				continue;
			}
	
			
			// Check Flag
			uint8_t flag = buffer[6];
			if (flag == 34) {
				return SEND_DATA;
			} else {
				continue;
			}			
	} else {
			count++;
		}
	}	
	
	//printf("WRITE_FILE_OK_ACT: Timed out waiting for File OK ACK.\n");
	return returnValue;
}

// -----SEND DATA STATE-----
STATE send_data_state(CircularQueue *window, ServerInfo *info) {
	// Initialize variables
	socklen_t clientLen = sizeof(info->clientAddr);
	uint32_t sequenceNum = 1;
	int eofReached = 0;
	int timeoutCount = 0;
	info->eofResendCount = 0;

	while (!eofReached) {
		// Fill the window while it is open
		while (!CircularQueue_is_full(window) && !eofReached) {
			
			// Read data from file
			uint8_t payload[MAXBUF];
			//int bytesRead = fread(payload, 1, MAXBUF, info->file);
			int bytesRead = fread(payload, 1, info->bufferSize, info->file); // 2nd change
			if (bytesRead <= 0) {
				eofReached = 1; // finsihed reading
				break;
			}
			
			// Create PDU (flag 16)
			uint8_t pduToSend[MAXBUF + 7];
			int pduLen = createPDU(pduToSend, sequenceNum, 16, payload, bytesRead);
	
			// Store in circular buffer
			CircularQueue_insert(window, sequenceNum, pduToSend, pduLen);
			printf("PDU LEN %d", pduLen);		
			// Send to client
			sendtoErr(info->childSocket, pduToSend, pduLen, 0, (struct sockaddr *)&(info->clientAddr), clientLen);
			sequenceNum++;

			// Check for RR/SREJ responses in non-blocking
			while (pollCall(0) > 0) {
				wait_on_ack_state(window, info);
			}

			timeoutCount = 0;
		}

		// Window is full, wait for space
		while(CircularQueue_is_full(window) && !eofReached) {
			int poll = pollCall(1000);
			if (poll > 0) {
				wait_on_ack_state(window, info);
				timeoutCount = 0;
			} else {
				// Timeout: resend oldest packet
				for (int i = 0; i < window->WindowSize; i++) {
					QueueEntry *entry = &window->entries[i];
					if (entry->valid) {
						// Create PDU flag 18
						uint32_t resentSeq;
						memcpy(&resentSeq, entry->packet, 4);
						resentSeq = ntohl(resentSeq);
						
						uint8_t timeoutPDU[MAXBUF + 7];
						int timeoutLen = createPDU(timeoutPDU, resentSeq, 18, entry->packet + 7, entry-> packetLen - 7);
						sendtoErr(info->childSocket, timeoutPDU, timeoutLen, 0, (struct sockaddr *)&(info->clientAddr), clientLen);
						//printf("[Server] Timeout: resent packet seq#%u flag 18.\n", resentSeq);
						break;
					}
				}
				timeoutCount++;
				if (timeoutCount >= 10) {
					//printf("ERROR: Timeouts >= 10, exiting.\n");
					return DONE;
				}
			}
		}
	}

	// -----Send EOF----- 
	if (eofReached) {
		uint8_t eofPDU[7]; // no payload
		int eofLen = createPDU(eofPDU, sequenceNum, 10, NULL, 0);
		sendtoErr(info->childSocket, eofPDU, eofLen, 0, (struct sockaddr *)&(info->clientAddr), clientLen);
		//printf("[Server] sent EOF packet with seq #%u (flag 10)\n", sequenceNum);

		// Save PDU to resend later		
		memcpy(info->eofPacket, eofPDU, eofLen);
		info->eofLen = eofLen;
		info->eofSeq = sequenceNum;
	}

	//CircularQueue_free(window);
	return WAIT_ON_EOF_ACK;
}


// ----- WAIT ON ACK STATE -----
STATE wait_on_ack_state(CircularQueue *window, ServerInfo *info) {
	uint8_t recvBuff[MAXBUF];
	int clientLen = sizeof(info->clientAddr);
	
	// Polling
	setupPollSet();
	addToPollSet(info->childSocket);
	int packetAckPoll = pollCall(1000);
	if (packetAckPoll <= 0) {
		if (packetAckPoll == 0) {
			printf("[Server] pollling timeout for RR/SREJ.\n");
		} else {
			printf("ERROR: poll failed.\n");
		}
		return SEND_DATA; // Send again
	}
	
	int bytesRecv = safeRecvfrom(info->childSocket, recvBuff, 7/*MAXBUF*/, 0, (struct sockaddr *)&(info->clientAddr), (int *)&clientLen);
	if (bytesRecv < 0) {
		printf("ERROR: failed to recv RR/SREJ");
		return DONE;
	}
		
	// Extract the flag and sequence number
	uint8_t flag = recvBuff[6];
	uint32_t ackSequence;
	memcpy(&ackSequence, recvBuff, 4);
	ackSequence = ntohl(ackSequence);

	// Check flag value
	if (flag == 5) { // RR
		printf("RR seq #%u\n", ackSequence);
		for (uint32_t i = 1; i < ackSequence; i++) {
			//CircularQueue_remove(window, ackSequence);
			CircularQueue_remove(window, i);
		}	
	} else if (flag == 6) { // SREJ
		printf("SREJ seq #%u\n", ackSequence);
		QueueEntry *entry = CircularQueue_get(window, ackSequence);
		if (entry) {
			uint8_t srejPDU[MAXBUF + 7];
			int srejLen = createPDU(srejPDU, ackSequence, 17, entry->packet + 7, entry->packetLen - 7);
			sendtoErr(info->childSocket, srejPDU, srejLen, 0, (struct sockaddr *)&(info->clientAddr), clientLen);
		}
	} else {
		printf("[Server] SREJ Unexpected Flag %d)\n", flag);
	}
	return SEND_DATA;
}


STATE wait_on_eof_ack_state(CircularQueue *window, ServerInfo *info) {
	uint8_t recvEofBuff[MAXBUF +7];
	socklen_t clientLen = sizeof(info->clientAddr);	
	
	int pollCallTimer = pollCall(1000);
	if (pollCallTimer > 0) {
		int bytesRecv = safeRecvfrom(info->childSocket, recvEofBuff, MAXBUF, 0, (struct sockaddr*)&(info->clientAddr), (int *)&clientLen);
		if (bytesRecv < 0) {
			//return RESEND_EOF;
			return WAIT_ON_EOF_ACK;
		}
	
  		uint8_t flag = recvEofBuff[6];
		uint32_t eofSequence;
		memcpy(&eofSequence, recvEofBuff, 4);
		eofSequence = ntohl(eofSequence);

		// Calculate checksum
		uint16_t recvChecksum;
		memcpy(&recvChecksum, recvEofBuff + 4, 2);	
		recvChecksum = ntohs(recvChecksum);


		// Compute checksum for validation
	    	uint8_t tempBuf[MAXBUF + 7];
                memcpy(tempBuf, recvEofBuff, bytesRecv);
                memset(tempBuf + 4, 0, 2); // zero checksum field
		uint16_t calcChecksum = in_cksum((unsigned short *)tempBuf, bytesRecv);
	        int checksum_valid = (recvChecksum == calcChecksum);
		                            

		if (flag == 35 && (eofSequence == info->eofSeq) && (checksum_valid)) {
			//printf("[Server] received EOF ACK (flag 35) for seq #%u.\n", eofSequence);
			return DONE;			
		} else { 
			//printf("[Server] unexpected flag %d while waiting for EOF ACK.\n", eofSequence);
			return WAIT_ON_EOF_ACK;
		}
	}
	
	// Poll timed out
	return RESEND_EOF;
}

STATE resend_eof_state(CircularQueue *window, ServerInfo *info) {
	if (info->eofResendCount >= 10) {
		//printf("[ERROR] EOF ACK not received after 10 attemped. Exiting...\n");
		return DONE;
	}

	// Resending EOF
	sendtoErr(info->childSocket, info->eofPacket, info->eofLen, 0, (struct sockaddr*)&(info->clientAddr), sizeof(info->clientAddr));
	info->eofResendCount++;
	//printf("[Server] resending EOF packet (attempt #%d)\n", info->eofResendCount);
	return WAIT_ON_EOF_ACK;
}










int checkArgs(int argc, char *argv[]) {
	// Checks args and returns port number
	int portNumber = 0;

	if ((argc > 3) || argc == 1) {
		fprintf(stderr, "Usage %s [error rate] [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 3) { // 2	{
		portNumber = atoi(argv[2]);
	}

	return portNumber;
}

float getErrorRate(int argc, char *argv[]) {
	// Check args and return error rate
	float errorRate = 0.0;

	if ((argc == 3) || (argc == 2)) {
		errorRate = atof(argv[1]);
		if (errorRate < 0 || errorRate >= 1) {
			fprintf(stderr, "Invalid error rate!\n");
			exit(-1);
		}
	}
	
	return errorRate;	
}



