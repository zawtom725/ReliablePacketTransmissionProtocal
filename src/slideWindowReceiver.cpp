/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * The sliding window protocal with the addition of:
 * 1. Buffer Control
 */
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>

#include <vector>
#include <iostream>

#include "./packet.h"
#include "./slideWindowReceiver.h"

using namespace std;


//////////////////////////
//  Macro
//////////////////////////


// debug flags
//#define _DEBUG
//#define _PRINT_WINDOW   1

// item state
#define ITEM_STATE_RECEIVED     0
#define ITEM_STATE_NOT_RECEIVED 1

// init window size, may only grow because of advertised window
// Ziang: for the 65 MB file at unlimited bandwidth, the adWindow is never used.
#define INIT_WINDOW_SIZE        200


//////////////////////////
//  Helper
//////////////////////////


void SlideWindowReceiver::ensureCorrectness(SlideWindowReceiverItem *item, int slideWindowPosition) {
  int seqNum = this->nextExpectedPacketSeqNum + slideWindowPosition;
  if (seqNum != item->seqNum) {
    fprintf(stderr, "SlideWindowReceiver: slideWindowItem error item %u at position %u\n",
            item->seqNum, seqNum);
    assert(false);
  }
}


//////////////////////////
//  Basic Implementation
//////////////////////////


SlideWindowReceiver::SlideWindowReceiver(int fileToWriteFd) {
  // file io
  this->fileToWriteFd = fileToWriteFd;

  // last seq num
  this->lastPacketSeqNumReceived = false;
  this->lastPacketSeqNum = 0;

  // we start at 0
  this->nextExpectedPacketSeqNum = 0;

  // prepare the buffer
  this->curWindowSize = INIT_WINDOW_SIZE;
  for (int i = 0; i < this->curWindowSize; i++) {
    SlideWindowReceiverItem *item = new SlideWindowReceiverItem();
    item->seqNum = this->nextExpectedPacketSeqNum + i;
    item->state = ITEM_STATE_NOT_RECEIVED;
    this->slideWindow.push_back(item);
  }
}


SlideWindowReceiver::~SlideWindowReceiver() {
  // clear the deque
  while (this->slideWindow.size() > 0) {
    SlideWindowReceiverItem *item = this->slideWindow[0];
    this->slideWindow.pop_front();
    delete item;
  }
}


bool SlideWindowReceiver::finished() {
  this->lock.lock();
  bool finished = this->lastPacketSeqNumReceived && 
                  (this->nextExpectedPacketSeqNum == this->lastPacketSeqNum + 1);
  this->lock.unlock();
  return finished;
}


//////////////////////////
//  Core Mechanism
//////////////////////////


AckPacket SlideWindowReceiver::receiveDataPacket(DataPacket *dataPacket) {
  this->lock.lock();

  // DataPacket type check
  assert(dataPacket->type == DATA_PACKET_TYPE_REGULAR ||
         dataPacket->type == DATA_PACKET_TYPE_FINISH);
  
  AckPacket retAckPacket;

  /// firstly, for FINISH data packet, set the lastPacketSeqNum;

  if (dataPacket->type == DATA_PACKET_TYPE_FINISH) {
    this->lastPacketSeqNum = dataPacket->seqNum;
    this->lastPacketSeqNumReceived = true;

    #ifdef _PRINT_WINDOW
    fprintf(stderr, "SlideWindowReceiver::receiveDataPacket set lastSeqNum [%u]\n",
            dataPacket->seqNum);
    #endif
  }

  /// then, if the DataPacket is within the buffer, get the data
  // other wise, reply cumulative==selective==nextPacketExpectedSeqNum

  if (dataPacket->seqNum < this->nextExpectedPacketSeqNum ||
      dataPacket->seqNum >= this->nextExpectedPacketSeqNum + this->curWindowSize) {
    // out of buffer
    retAckPacket.cumulativeSeqNum = this->nextExpectedPacketSeqNum;
    retAckPacket.selectiveSeqNum = this->nextExpectedPacketSeqNum;

    #ifdef _PRINT_WINDOW
    fprintf(stderr, "SlideWindowReceiver::receiveDataPacket out or range data packet [%u] reply cumu=select [%u]\n",
            dataPacket->seqNum, this->nextExpectedPacketSeqNum);
    #endif
  } else {
    // within buffer
    /// 1. buf the data
    
    int i = dataPacket->seqNum - this->nextExpectedPacketSeqNum;
    SlideWindowReceiverItem *item = this->slideWindow[i];
    if (item->state == ITEM_STATE_NOT_RECEIVED) {
      // write to file
      item->state = ITEM_STATE_RECEIVED;
      lseek(this->fileToWriteFd, item->seqNum * DATA_PACKET_DATA_SIZE, SEEK_SET);
      write(this->fileToWriteFd, dataPacket->data, dataPacket->dataSize);
    }

    retAckPacket.selectiveSeqNum = dataPacket->seqNum;
    
    /// 2.slide the window

    while (this->slideWindow.size() > 0 && this->slideWindow[0]->state == ITEM_STATE_RECEIVED) {
      SlideWindowReceiverItem *item = this->slideWindow[0];
      this->slideWindow.pop_front();

      #ifdef _DEBUG
      this->ensureCorrectness(item, 0);
      #endif

      #ifdef _PRINT_WINDOW
      fprintf(stderr, "SlideWindowReceiver::receiveDataPacket slide pass [%u]\n",
              item->seqNum);
      #endif

      delete item;

      // advance the nextExpectedPacketSeqNum
      this->nextExpectedPacketSeqNum += 1;
    }

    retAckPacket.cumulativeSeqNum = this->nextExpectedPacketSeqNum;

    #ifdef _PRINT_WINDOW
    fprintf(stderr, "SlideWindowReceiver::receiveDataPacket in range data packet [%u] reply cumu [%u] select [%u]\n",
            dataPacket->seqNum, retAckPacket.cumulativeSeqNum, retAckPacket.selectiveSeqNum);
    #endif
  }

  /// advertised window
  // disabled, curWindowSize is always at INIT_WINDOW_SIZE
  // this->curWindowSize = max(this->curWindowSize, dataPacket->adWindow);

  /// buffer control

  for (int i = this->slideWindow.size(); i < this->curWindowSize; i++) {
    SlideWindowReceiverItem *item = new SlideWindowReceiverItem();
    item->seqNum = this->nextExpectedPacketSeqNum + i;
    item->state = ITEM_STATE_NOT_RECEIVED;
    this->slideWindow.push_back(item);

    #ifdef _PRINT_WINDOW
    fprintf(stderr, "SlideWindowReceiver::receiveDataPacket slide reach [%u]\n",
            item->seqNum);
    #endif
  }

  this->lock.unlock();
  return retAckPacket;
}