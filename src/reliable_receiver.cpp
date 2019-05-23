/*
 * UIUC SP19 CS 438 MP3
 * Author: Jiacheng Zhu, Ziang Wan
 * The main file for reliable receiver.
 *
 * List of algorithms used in the sender:
 * 1. Sliding Window Protocal with Advertised Window
 * 2. FIN - ACK one-way tear down
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>

#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <thread>
#include <limits.h>

#include "./packet.h"
#include "./slideWindowReceiver.h"

using namespace std;


///////////////////////////////
/// defs
///////////////////////////////


// debug flag
// #define _PROFILING      1
// #define _PRINT_CONTROL  1
// #define _PRINT_PACKET   1
// #define _LOCAL_TEST     1

// socket timeout
#define SOCKET_TIMEOUT_US   (50 * 1000)

// number of receiving thread
#define RECEIVE_THREAD_NUM    2

// the ACK for FINISH data packet should be sent multiple times
#define FIN_ACK_NUM   4


///////////////////////////////
/// globals
///////////////////////////////


// the slide window on the receiver
SlideWindowReceiver *slideWindow;

// socket and file descriptor
int transferSocket;
int fileToWriteFd;

// profiling
#ifdef _PROFILING
unsigned int _profilingHandshakeReceived = 0;
unsigned int _profilingDataPacketReceived = 0;
unsigned long _profilingProcessingTotalTime = 0;
#endif

///////////////////////////////
/// helpers
///////////////////////////////


unsigned long getElapsedUs(const struct timeval &before,
  const struct timeval &after) {
  unsigned long us = (after.tv_sec - before.tv_sec) * 1000 * 1000 +
                     (after.tv_usec - before.tv_usec);
  return us;
}


int createfileToWriteFd(char *filePathToRead) {
  fileToWriteFd = open(filePathToRead, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fileToWriteFd < 0) {
    perror("socket");
    return -1;
  }
  return fileToWriteFd;
}


int createTransferSocket(unsigned short int rcvPort) {
  int retCode;

  transferSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (transferSocket < 0) {
    perror("socket");
    return -1;
  }

  // set the socket to a timeout for receiving
  struct timeval timeoutStruct;
  timeoutStruct.tv_sec = 0;
  timeoutStruct.tv_usec = SOCKET_TIMEOUT_US;
  setsockopt(transferSocket, SOL_SOCKET, SO_RCVTIMEO, &timeoutStruct, sizeof(struct timeval));
  // bind the socket to all local interface at port rcvPort for receiving
  struct sockaddr_in bindAddr;
  memset(&bindAddr, 0, sizeof(bindAddr));
  bindAddr.sin_family = AF_INET;
  // NOTE: use the same port as the receiver for receiving on sender, might cause a bug
  bindAddr.sin_port = htons(rcvPort);

  //bindAddr.sin_addr.s_addr = INADDR_ANY;
  #ifdef _LOCAL_TEST
  inet_pton(AF_INET, "127.0.0.2", &bindAddr.sin_addr);
  #else
  bindAddr.sin_addr.s_addr = INADDR_ANY;
  #endif
  
  retCode = bind(transferSocket, (struct sockaddr *) &bindAddr, sizeof(struct sockaddr_in));
  if(retCode < 0) {
    close(transferSocket);
    perror("bind");
    return -1;
  }

  return transferSocket;
}


///////////////////////////////
/// ReceiveFrame and Ack - multiplicity = *
///////////////////////////////


void receiveDataReplyAck() {
  /// keep receiving until we got all the data up to the last DATA packet
  // if we receive a data packet with the header DATA_PACKET_HEADER_FINISH.
  // we know that it's the last packet

  // get the address of the sender
  struct sockaddr_in theirAddr;
  socklen_t theirAddrLen;

  char buffer[DATA_PACKET_SIZE];
  ssize_t bufferContentLength;

  DataPacket dataPacket;
  AckPacket ackPacket;

  #ifdef _PROFILING
  struct timeval before;
  struct timeval after;
  #endif

  while(!slideWindow->finished()){
    // try to read a data packet
    memset(&buffer, 0, DATA_PACKET_SIZE);
    memset(&dataPacket, 0, sizeof(DataPacket));

    bufferContentLength = recvfrom(transferSocket, buffer, DATA_PACKET_SIZE , 0,
                                   (struct sockaddr*) &theirAddr, &theirAddrLen);
    if (bufferContentLength < 0) {
      continue;
    }
  
    #ifdef _PROFILING
    gettimeofday(&before, NULL);
    #endif

    readBufferDataPacket(&dataPacket, buffer, bufferContentLength);

    if (dataPacket.type == DATA_PACKET_TYPE_INVALID) {
      // the type is DATA_PACKET_TYPE_INVALID
      // in this case we do nothing
      continue;
    }

    #ifdef _PRINT_PACKET
    fprintf(stderr, "reliable_receiver::receiveDataReplyAck receive packet type [%u] seq [%u] dataSize [%d]\n",
            dataPacket.type, dataPacket.seqNum, dataPacket.dataSize);
    #endif

    // avoid corruption between iterations
    memset(&ackPacket, 0, sizeof(AckPacket));

    if (dataPacket.type == DATA_PACKET_TYPE_HANDSHAKE) {
      // we receive a handshake, in this case, reply a handshake ack

      #ifdef _PRINT_PACKET
      fprintf(stderr, "reliable_receiver::receiveDataReplyAck reply handshake\n");
      // dataPacket.sentTime.tv_sec, dataPacket.sentTime.tv_usec);
      #endif

      #ifdef _PROFILING
      _profilingHandshakeReceived += 1;
      #endif

      ackPacket.type = ACK_PACKET_TYPE_HANDSHAKE;
      ackPacket.cumulativeSeqNum = 0;           // not used in the sender
      ackPacket.selectiveSeqNum = 0;            // not used in the sender
      ackPacket.sentTime = dataPacket.sentTime; // used in the sender to determine initTimeoutUs
    } else if (dataPacket.type == DATA_PACKET_TYPE_REGULAR ||
               dataPacket.type == DATA_PACKET_TYPE_FINISH) {
      // we receive a data packet, let the sliding window handles

      #ifdef _PRINT_PACKET
      fprintf(stderr, "reliable_receiver::receiveDataReplyAck reply dataPacket type [%u] seq [%u] dataSize [%d]\n",
              dataPacket.type, dataPacket.seqNum, dataPacket.dataSize);
      #endif

      #ifdef _PROFILING
      _profilingDataPacketReceived += 1;
      #endif

      ackPacket = slideWindow->receiveDataPacket(&dataPacket);
      ackPacket.type = ACK_PACKET_TYPE_REGULAR;
      ackPacket.sentTime = dataPacket.sentTime;
    }

    // reuse the buffer to send the ACK
    size_t acksize = fillBufferAckPacket(&ackPacket, buffer);

    // if it is the finishing ACK, send it multiple times
    if (slideWindow->finished()) {
      for (int i = 0; i < FIN_ACK_NUM; i++) {
        sendto(transferSocket, buffer, acksize, 0,
               (struct sockaddr*) &theirAddr, sizeof(theirAddr));
      }
    } else {
      sendto(transferSocket, buffer, acksize, 0,
             (struct sockaddr*) &theirAddr, sizeof(theirAddr));
    }

    
    #ifdef _PROFILING
    gettimeofday(&after, NULL);
    _profilingProcessingTotalTime += getElapsedUs(before, after);
    #endif
  }

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_receiver::receiveDataReplyAck terminates\n");
  #endif
}


///////////////////////////////
/// Primary Helper
///////////////////////////////


int reliablyReceive(unsigned short int rcvPort, char* fileToWrite) {
  int retCode;

  retCode = createfileToWriteFd(fileToWrite);
  if (retCode < 0) {
    return -1;
  }

  retCode = createTransferSocket(rcvPort);
  if (retCode < 0) {
    return -1;
  }

  // create the sliding window receiver
  slideWindow = new SlideWindowReceiver(fileToWriteFd);

  // do work
  thread receiveDataReplyAckThreads[RECEIVE_THREAD_NUM];
  for (int i = 0; i < RECEIVE_THREAD_NUM; i++) {
    receiveDataReplyAckThreads[i] = thread(receiveDataReplyAck);
  }

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_receiver::All threads started\n");
  #endif

  // finish work
  for (int i = 0; i < RECEIVE_THREAD_NUM; i++) {
    receiveDataReplyAckThreads[i].join();
  }

  // free resource
  close(fileToWriteFd);
  close(transferSocket);

  delete slideWindow;

  #ifdef _PROFILING
  fprintf(stderr, "reliable_sender::profile handshakes [%u] packets [%u] time [%lu]\n",
          _profilingHandshakeReceived, _profilingDataPacketReceived, _profilingProcessingTotalTime);
  #endif

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_receiver::Receiving succeeds\n");
  #endif

  return 0;
}


///////////////////////////////
/// Main
///////////////////////////////


int main(int argc, char** argv) {
  // parse arguments
  if(argc != 3) {
    fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
    exit(1);
  }

  unsigned short int rcvPort = (unsigned short int) atoi(argv[1]);

  // invoke the primary helper
  return reliablyReceive(rcvPort, argv[2]);
}