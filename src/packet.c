/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * Handle Packet Marshaling and Unmarshaling
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include "./packet.h"


//////////////////////////
// Data Packet
//////////////////////////


size_t fillBufferDataPacket(struct DataPacket *dataPacket, char *buffer) {
  // fill in the buffer, assume that the buffer is large enough (>= DATA_PACKET_SIZE)
  // NOTE: since we are communicating from VM to another VM, there is no endianness concern
  
  // type
  if (dataPacket->type == DATA_PACKET_TYPE_HANDSHAKE) {
    buffer[0] = 'h';
    buffer[1] = 's';
  } else if (dataPacket->type == DATA_PACKET_TYPE_REGULAR) {
    buffer[0] = 'r';
    buffer[1] = 'e';
  } else if (dataPacket->type == DATA_PACKET_TYPE_FINISH) {
    buffer[0] = 'f';
    buffer[1] = 'i';
  } else {
    fprintf(stderr, "dataPacket invalid type\n");
    assert(false);
  }

  // sequence number
  unsigned int *seqNumPosition = (unsigned int *) (&buffer[2]);
  *seqNumPosition = dataPacket->seqNum;

  // sentTime
  struct timeval *sentTimePosition = (struct timeval *) (&buffer[6]);
  *sentTimePosition = dataPacket->sentTime;

  // data
  memcpy(&buffer[22], dataPacket->data, dataPacket->dataSize);

  size_t supposedSize = DATA_PACKET_HEADER_SIZE + dataPacket->dataSize;
  return supposedSize;
}


void readBufferDataPacket(struct DataPacket *dataPacket, char *buffer,
  ssize_t bufferContentLength) {
  // size checking
  assert(bufferContentLength <= DATA_PACKET_SIZE &&
         bufferContentLength >= DATA_PACKET_HEADER_SIZE);

  // type
  if (buffer[0] == 'h' && buffer[1] == 's') {
    dataPacket->type = DATA_PACKET_TYPE_HANDSHAKE;
  } else if (buffer[0] == 'r' && buffer[1] == 'e') {
    dataPacket->type = DATA_PACKET_TYPE_REGULAR;
  } else if (buffer[0] == 'f' && buffer[1] == 'i') {
    dataPacket->type = DATA_PACKET_TYPE_FINISH;
  } else {
    dataPacket->type = DATA_PACKET_TYPE_INVALID;
    return;
  }

  // sequence number
  unsigned int *seqNumPosition = (unsigned int *) (&buffer[2]);
  dataPacket->seqNum = *seqNumPosition;

  // sentTime
  struct timeval *sentTimePosition = (struct timeval *) (&buffer[6]);
  dataPacket->sentTime = *sentTimePosition;

  // size
  dataPacket->dataSize = bufferContentLength - DATA_PACKET_HEADER_SIZE;

  // data
  // memcpy(dataPacket->data, &(buffer[22]), dataPacket->dataSize);

  dataPacket->data = &(buffer[22]);
}


//////////////////////////
// ACK Packet
//////////////////////////


size_t fillBufferAckPacket(struct AckPacket *ackPacket, char *buffer) {
  // fill in the buffer, assume that the buffer is large enough (>= ACK_PACKET_SIZE)
  // NOTE: since we are communicating from VM to another VM, there is no endianness concern
  
  // type
  if (ackPacket->type == ACK_PACKET_TYPE_HANDSHAKE) {
    buffer[0] = 'h';
    buffer[1] = 's';
  } else if (ackPacket->type == ACK_PACKET_TYPE_REGULAR) {
    buffer[0] = 'r';
    buffer[1] = 'e';
  } else {
    fprintf(stderr, "ackPacket invalid type\n");
    assert(false);
  }

  // cumulative sequence number
  unsigned int *cumulativeSeqNumPosition = (unsigned int *) (&buffer[2]);
  *cumulativeSeqNumPosition = ackPacket->cumulativeSeqNum;

  // selective sequence number
  unsigned int *selectiveSeqNumPosition = (unsigned int *) (&buffer[6]);
  *selectiveSeqNumPosition = ackPacket->selectiveSeqNum;

  // sentTime
  struct timeval *sentTimePosition = (struct timeval *) (&buffer[10]);
  *sentTimePosition = ackPacket->sentTime;

  size_t supposedSize = ACK_PACKET_SIZE;
  return supposedSize;
}


void readBufferAckPacket(struct AckPacket *ackPacket, char *buffer,
  ssize_t bufferContentLength) {
  // check size
  assert(bufferContentLength == ACK_PACKET_SIZE);

  // type
  if (buffer[0] == 'h' && buffer[1] == 's') {
    ackPacket->type = ACK_PACKET_TYPE_HANDSHAKE;
  } else if (buffer[0] == 'r' && buffer[1] == 'e') {
    ackPacket->type = ACK_PACKET_TYPE_REGULAR;
  } else {
    ackPacket->type = ACK_PACKET_TYPE_INVALID;
    return;
  }

  // cumulative sequence number
  unsigned int *cumulativeSeqNumPosition = (unsigned int *) (&buffer[2]);
  ackPacket->cumulativeSeqNum = *cumulativeSeqNumPosition;

  // selective sequence number
  unsigned int *selectiveSeqNumPosition = (unsigned int *) (&buffer[6]);
  ackPacket->selectiveSeqNum = *selectiveSeqNumPosition;

  // sentTime
  struct timeval *sentTimePosition = (struct timeval *) (&buffer[10]);
  ackPacket->sentTime = *sentTimePosition;
}
