# Remote-File-Copy-UDP-Program

This project implements a reliable file transfer system over UDP using a Selective Reject Automatic Repeat reQuest (ARQ) protocol with a sliding window flow control mechanism.
The system consists of C program:

server – acts as the sender, reading a file and transmitting it to the client in packets.
rcopy – acts as the receiver, downloading the file from the server and writing it to disk.

1. server (Sender)
  Reads the specified source file (from-filename) from its local directory.
  Breaks the file into fixed-size data packets, each with a custom application-level header.
  Transmits packets over UDP to the connected client.
  Maintains a sliding window of outstanding packets awaiting acknowledgment (ACK or SREJ).
  Retransmits lost or corrupted packets as indicated by Selective Reject (SREJ) messages from the client.
  Sends an End-of-File (EOF) packet once all data has been successfully transmitted.

2. rcopy (Receiver)
  Initiates a connection by sending a request to the server with:
  The source filename (from-filename) on the server.
  The destination filename (to-filename) to save the downloaded file locally.
  Creates the output file and prepares to receive data packets.
  Validates packet sequence numbers and detects missing or out-of-order packets.
  Sends Receiver Ready (RR) or Selective Reject (SREJ) responses based on received packet order.
  Buffers out-of-order packets until missing ones are received.
  Reassembles the complete file in order and writes it to disk.
