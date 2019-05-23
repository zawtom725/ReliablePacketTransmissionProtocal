/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * Handle Packet Marshaling and Unmarshaling
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#ifndef PACKET_H
#define PACKET_H


//////////////////////////
// Macros
//////////////////////////


// mtu and ip/udp
#define MTU                       1500
#define IP_AND_UDP_HEADER_SIZE    28

// data packet
#define DATA_PACKET_SIZE          (MTU - IP_AND_UDP_HEADER_SIZE)
#define DATA_PACKET_HEADER_SIZE   22
#define DATA_PACKET_DATA_SIZE     (DATA_PACKET_SIZE - DATA_PACKET_HEADER_SIZE)

// data packet header
#define DATA_PACKET_TYPE_INVALID    (-1)
#define DATA_PACKET_TYPE_HANDSHAKE  0
#define DATA_PACKET_TYPE_REGULAR    1
#define DATA_PACKET_TYPE_FINISH     2

// ack packet
#define ACK_PACKET_SIZE   26

// ack packet header
#define ACK_PACKET_TYPE_INVALID     (-1)
#define ACK_PACKET_TYPE_HANDSHAKE   0
#define ACK_PACKET_TYPE_REGULAR     1


//////////////////////////
// Data Packet
//////////////////////////


// the following struct is just a placeholder for
// all the information needed for a data packet
struct DataPacket {
  // see the macros defined above
  short type;
  // the sequence number starting at 0
  unsigned int seqNum;
  // for RttEstimation
  struct timeval sentTime;
  // usually is 1450 bytes but the last one could be shorter
  // dataSize won't be sent through the network, but is determined by
  // the length of received packet
  int dataSize;
  // the storage contains binary data
  //char data[DATA_PACKET_DATA_SIZE];
  char* data;
};


// assume the buffer is large enough (>= DATA_PACKET_SIZE)
size_t fillBufferDataPacket(struct DataPacket *dataPacket, char *buffer);


// assume that the size of data is valid
void readBufferDataPacket(struct DataPacket *dataPacket, char *buffer,
  ssize_t bufferContentLength);


//////////////////////////
// ACK Packet
//////////////////////////


// same as DataPacket, a placeholder
struct AckPacket {
  // see the macros defined above
  short type;
  // cumulative ack = next expected packet
  unsigned int cumulativeSeqNum;
  // selective ack = this packet received
  unsigned int selectiveSeqNum;
  // for Rtt estimation
  struct timeval sentTime;
};


// assume the buffer is large enough (>= ACK_PACKET_SIZE)
size_t fillBufferAckPacket(struct AckPacket *ackPacket, char *buffer);


// assume that the size of data is valid
void readBufferAckPacket(struct AckPacket *ackPacket, char *buffer,
  ssize_t bufferContentLength);


#endif
