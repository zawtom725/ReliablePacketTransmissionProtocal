/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * The sliding window protocal with the addition of:
 * 1. Selective Repeat with both cumulative and selective acks
 * 2. TCP Slow Start, Congestion Control
 * 3. Fast Recovery and Fast Retransmission
 * 4. Buffer Control
 *
 * Arguably, this is the hardest part of this MP.
 */
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#include <vector>
#include <iostream>
#include <sstream>

#include "./packet.h"
#include "./slideWindowSender.h"

using namespace std;


//////////////////////////
//  Macro
//////////////////////////


// debug flags
//#define _DEBUG
//#define _PRINT_WINDOW   1

// item state
#define ITEM_STATE_NOT_SENT 0
#define ITEM_STATE_SENT     1
#define ITEM_STATE_TIMEOUT  2
#define ITEM_STATE_RESENT   3
#define ITEM_STATE_ACKED    4

// fast retransmission
// #define FAST_RETRANSMISSION_TRESHOLD   (1 + this->slideWindow.size() / 2)
#define FAST_RETRANSMISSION_THRESHOLD     2

// the minimum congestion window
#define CONGESTION_WINDOW_MIN         10
#define CONGESTION_THRESHOLD_MIN      20
// the congestion threshold will shrink to % of window if timeout happens
#define CONGESTION_THRESHOLD_SHRINK   0.62


//////////////////////////
//  Helper
//////////////////////////


void SlideWindowSender::readFile(SlideWindowSenderItem *item,
  unsigned int seqNum, int size) {
  lseek(this->fileToSendFd, seqNum * DATA_PACKET_DATA_SIZE, SEEK_SET);
  // read might read less data for the last packet
  ssize_t bytesRead = read(this->fileToSendFd, item->data, size);
  if (bytesRead < 0) {
    fprintf(stderr, "SlideWindowSender::readFile failed at position [%u]\n",
            seqNum);
    assert(false);
  }
  item->dataSize = bytesRead;

  #ifdef _PRINT_WINDOW
  fprintf(stderr, "SlideWindowSender::readFile read [%lu] bytes at [%u]\n",
          bytesRead, seqNum);
  #endif
}


void SlideWindowSender::ensureCorrectness(SlideWindowSenderItem *item, int slideWindowPosition) {
  int seqNum = this->slideWindowLeftBoundSeqNum + slideWindowPosition;
  if (seqNum != item->seqNum) {
    fprintf(stderr, "SlideWindowSender: slideWindowItem error item %u at position %u\n",
            item->seqNum, seqNum);
    assert(false);
  }
}


string stateToString(int state) {
  if (state == ITEM_STATE_ACKED) {
    return "acked";
  } else if (state == ITEM_STATE_NOT_SENT) {
    return "notsent";
  } else if (state == ITEM_STATE_RESENT) {
    return "resent";
  } else if (state == ITEM_STATE_SENT) {
    return "sent";
  } else if (state == ITEM_STATE_TIMEOUT) {
    return "timeout";
  } else {
    return "invalid";
  }
}


void SlideWindowSender::printContent() {
  stringstream ss;
  ss << "SlideWindowSender::current congestion " << this->curCongestionWindow << endl;
  ss << "SlideWindowSender::current slide:";
  for (int i = 0; i < this->slideWindow.size(); i++) {
    SlideWindowSenderItem *item = this->slideWindow[i];
    ss << " (" << item->seqNum << ", s=" << stateToString(item->state) << ")";
  }
  ss << endl;
  cerr << ss.str() << endl;
}


//////////////////////////
//  Basic Implementation
//////////////////////////


SlideWindowSender::SlideWindowSender(int fileToSendFd,
    uint64_t bytesToSend, bool fastRetransmission) {
  // file io
  this->fileToSendFd = fileToSendFd;

  // completion tracking
  this->bytesToSend = bytesToSend;
  this->bytesSent = 0;

  // convenience lastPacket info
  if (bytesToSend % DATA_PACKET_DATA_SIZE == 0) {
    this->lastPacketSeqNum = bytesToSend / DATA_PACKET_DATA_SIZE - 1;
    this->lastPacketSize = DATA_PACKET_DATA_SIZE;
  } else {
    this->lastPacketSeqNum = bytesToSend / DATA_PACKET_DATA_SIZE;
    this->lastPacketSize = this->bytesToSend - this->lastPacketSeqNum * DATA_PACKET_DATA_SIZE;
  }
  
  // fast retransmission
  this->fastRetransmission = fastRetransmission;
  this->lastAck = 0;
  this->lastAckAckedCount = 0;

  // we start at sequence number 0 with a congestion window size of 2
  // in the beginning, there is no threshold so we keep doubling
  this->slideWindowLeftBoundSeqNum = 0;
  this->curCongestionWindow = CONGESTION_WINDOW_MIN;
  this->congestionLeftBoundSeqNum = this->slideWindowLeftBoundSeqNum + this->curCongestionWindow;
  this->curCongestionThreshold = UINT_MAX;

  // fill the queue with the first two packet
  for (int i = 0; i < this->curCongestionWindow && i <= this->lastPacketSeqNum; i++) {
    SlideWindowSenderItem *item = new SlideWindowSenderItem();
    item->seqNum = i;
    item->state = ITEM_STATE_NOT_SENT;
    if (item->seqNum == this->lastPacketSeqNum) {
      this->readFile(item, i, this->lastPacketSize);
    } else {
      this->readFile(item, i, DATA_PACKET_DATA_SIZE);
    }
    this->slideWindow.push_back(item);
  }

  // mark that there is new data to be sent
  this->newDataReady = true;

  // no timeout has happened
  this->curCongestionWindowTimeouted = false;
}


SlideWindowSender::~SlideWindowSender() {
  // clear the deque
  while (this->slideWindow.size() > 0) {
    SlideWindowSenderItem *item = this->slideWindow[0];
    this->slideWindow.pop_front();
    delete item;
  }
}


bool SlideWindowSender::finished() {
  this->lock.lock();
  bool finished = (this->bytesSent == this->bytesToSend);
  this->lock.unlock();
  return finished;
}


bool SlideWindowSender::hasDataToSend() {
  this->lock.lock();
  bool finished = (this->bytesSent == this->bytesToSend);
  bool hasDataToSend = (!finished) && this->newDataReady;
  this->lock.unlock();
  return hasDataToSend;
}


//////////////////////////
//  Core Mechanism
//////////////////////////


// time complexity: O(N)
vector<DataPacket *> SlideWindowSender::prepareDataPacketsToSend() {
  this->lock.lock();

  vector<DataPacket *> retVec;

  struct timeval now;
  gettimeofday(&now, NULL);

  // iterate through the deque
  // if an item is of the state NOT_SENT or TIMEOUT, it is a new data packet to send
  for (int i = 0; i < this->curCongestionWindow && i < this->slideWindow.size(); i++) {
    SlideWindowSenderItem *item = this->slideWindow[i];
    unsigned int seqNum = this->slideWindowLeftBoundSeqNum + i;

    if (item->state == ITEM_STATE_NOT_SENT || item->state == ITEM_STATE_TIMEOUT) {
      // correctness check
      #ifdef _DEBUG
      this->ensureCorrectness(item, i);
      #endif

      // prepare the data packet
      DataPacket *dp = new DataPacket();
      dp->type = item->seqNum == this->lastPacketSeqNum ? DATA_PACKET_TYPE_FINISH : DATA_PACKET_TYPE_REGULAR;
      dp->seqNum = item->seqNum;
      dp->sentTime = now;
      dp->dataSize = item->dataSize;
      dp->data = item->data;

      retVec.push_back(dp);

      #ifdef _PRINT_WINDOW
      fprintf(stderr, "SlideWindowSender::prepareDataPacketsToSend send [%u] with state [%d]\n",
              item->seqNum, item->state);
      #endif

      // state change: notsent -> sent; timeout -> resent
      if (item->state == ITEM_STATE_NOT_SENT) {
        item->state = ITEM_STATE_SENT;
      } else {
        item->state = ITEM_STATE_RESENT;
      }

      // timing
      item->lastSentTime = now;
    }
  }

  // indicate that at current state, all data packet has been sent
  this->newDataReady = false;
  // indicate that we can have another round of timeout / fast retransmission
  this->curCongestionWindowTimeouted = false;

  this->lock.unlock();

  return retVec;
}


// time complexity: O(N)
void SlideWindowSender::ackArrived(AckPacket *ackPacket) {
  this->lock.lock();

  #ifdef _PRINT_WINDOW
  fprintf(stderr, "SlideWindowSender::ackArrived packet cumulative [%u] selective [%u]\n",
          ackPacket->cumulativeSeqNum, ackPacket->selectiveSeqNum);
  #endif

  /// handles the selective ack
  // NOTE: per Jiacheng, if cumulativeSeqNum == selectiveSeqNum, it means that
  //       the receiver receives out of buffer packet. In that case, ignore
  //       the selective field

  if (ackPacket->cumulativeSeqNum != ackPacket->selectiveSeqNum) {
    if (ackPacket->selectiveSeqNum >= this->slideWindowLeftBoundSeqNum) {
      int i = ackPacket->selectiveSeqNum - this->slideWindowLeftBoundSeqNum;
      if (i < this->slideWindow.size()) {
        SlideWindowSenderItem *item = this->slideWindow[i];

        // correctness check
        #ifdef _DEBUG
        this->ensureCorrectness(item, i);
        #endif

        item->state = ITEM_STATE_ACKED;
      }
    }
  }

  /// handles fast retransmission

  if (this->fastRetransmission) {
    if (this->lastAck == ackPacket->cumulativeSeqNum) {
      this->lastAckAckedCount += 1;

      // only retransmit one time
      if (this->lastAckAckedCount == FAST_RETRANSMISSION_THRESHOLD) {
        // to avoid repeated punishment
        if (!this->curCongestionWindowTimeouted) {
          // congestion control
          this->curCongestionThreshold = (unsigned int) (this->curCongestionWindow * CONGESTION_THRESHOLD_SHRINK);
          if (this->curCongestionThreshold < CONGESTION_THRESHOLD_MIN) {
            this->curCongestionThreshold = CONGESTION_THRESHOLD_MIN;
          }
          this->curCongestionWindow = this->curCongestionThreshold;

          // set new left bound
          this->congestionLeftBoundSeqNum = this->slideWindowLeftBoundSeqNum + this->curCongestionWindow;

          // set the flag to avoid repeated punishment
          this->curCongestionWindowTimeouted = true;
        }

        #ifdef _PRINT_WINDOW
        fprintf(stderr, "SlideWindowSender::checkTimeout congestion shrink to [%u] threshold [%u] leftBound [%u]\n",
                this->curCongestionWindow, this->curCongestionThreshold, this->congestionLeftBoundSeqNum);
        #endif

        // set sent/resent to timeout for the first item

        if (this->slideWindow.size() > 0) {
          SlideWindowSenderItem *item = this->slideWindow[0];
          if (item->state == ITEM_STATE_SENT || item->state == ITEM_STATE_RESENT) {
            item->state = ITEM_STATE_TIMEOUT;
            this->newDataReady = true;

            #ifdef _PRINT_WINDOW
            fprintf(stderr, "SlideWindowSender::ackArrived fast retrans [%u]\n",
                    item->seqNum);
            #endif
          }
        }
      }
    } else if (this->lastAck < ackPacket->cumulativeSeqNum) {
      this->lastAck = ackPacket->cumulativeSeqNum;
      this->lastAckAckedCount = 0;
    }
  }

  /// handles the cumulative sequence num
  // if cumulativeSeqNum = 5, it means that packets 0,1,2,3,4 have all been received
  // right now, the receiver is waiting for packet 5.

  if (ackPacket->cumulativeSeqNum > this->slideWindowLeftBoundSeqNum) {

    #ifdef _DEBUG
    if (ackPacket->cumulativeSeqNum - this->slideWindowLeftBoundSeqNum > this->slideWindow.size()) {
      fprintf(stderr, "SlideWindowSender::ackArrived not enough data to slide through cumu [%u] lB [%u] sS [%lu]\n",
              ackPacket->cumulativeSeqNum, this->slideWindowLeftBoundSeqNum, this->slideWindow.size());
      assert(false);
    }
    #endif

    // slide window
    for (int i = this->slideWindowLeftBoundSeqNum; i < ackPacket->cumulativeSeqNum; i++) {
      // pop the first element
      SlideWindowSenderItem *item = this->slideWindow[0];
      this->slideWindow.pop_front();

      // correctness check
      #ifdef _DEBUG
      this->ensureCorrectness(item, i - this->slideWindowLeftBoundSeqNum);
      #endif

      // update bytes acked
      this->bytesSent += item->dataSize;

      #ifdef _PRINT_WINDOW
      fprintf(stderr, "SlideWindowSender::ackArrived slide pass: [%u] of dataSize [%d]\n",
              item->seqNum, item->dataSize);
      #endif

      // memory management
      delete item;
    }

    this->slideWindowLeftBoundSeqNum = ackPacket->cumulativeSeqNum;
  }

  /// then, handles congestion control
  // if the congestionLeftBoundSeqNum is at the left of slideWindowLeftBoundSeqNum
  // increase curCongestionWindow
  // only grow once

  if (this->slideWindowLeftBoundSeqNum >= this->congestionLeftBoundSeqNum) {
    // grow the congestion window
    if (this->curCongestionWindow < this->curCongestionThreshold) {
      this->curCongestionWindow *= 2;
      if (this->curCongestionWindow >= this->curCongestionThreshold) {
        this->curCongestionWindow = this->curCongestionThreshold;
      }
    } else {
      this->curCongestionWindow += CONGESTION_THRESHOLD_MIN;
    }

    // set new left bound
    this->congestionLeftBoundSeqNum = this->slideWindowLeftBoundSeqNum + this->curCongestionWindow;
  
    #ifdef _PRINT_WINDOW
    fprintf(stderr, "SlideWindowSender::ackArrived new congestion window [%u] leftBound [%u]\n",
            this->curCongestionWindow, this->congestionLeftBoundSeqNum);
    #endif
  }

  /// fill in the deque if currently the slideWindow is smaller than the congestion window

  for (int i = this->slideWindow.size(); i < this->curCongestionWindow; i++) {
    int seqNum = this->slideWindowLeftBoundSeqNum + i;
    if (seqNum > this->lastPacketSeqNum) {
      break;
    }

    SlideWindowSenderItem *item = new SlideWindowSenderItem();
    item->seqNum = seqNum;
    item->state = ITEM_STATE_NOT_SENT;
    if (item->seqNum == this->lastPacketSeqNum) {
      this->readFile(item, seqNum, this->lastPacketSize);
    } else {
      this->readFile(item, seqNum, DATA_PACKET_DATA_SIZE);
    }

    this->slideWindow.push_back(item);

    #ifdef _PRINT_WINDOW
    fprintf(stderr, "SlideWindowSender::ackArrived slide reach: [%u]\n",
            seqNum);
    #endif 
  }

  /// unfortunately, our clever way of handling timeout does not work
  // there are too many corner cases, Ziang: I decided to do a simple traverse here

  if (!this->newDataReady) {
    for (int i = 0; i < this->slideWindow.size() && i < this->curCongestionWindow; i++) {
      SlideWindowSenderItem *item = this->slideWindow[i];
      if (item->state == ITEM_STATE_NOT_SENT || item->state == ITEM_STATE_TIMEOUT) {
        this->newDataReady = true;
        break;
      }
    }
  }

  this->lock.unlock();
}


// time complexity: O(N)
void SlideWindowSender::checkTimeout(unsigned long timeoutThresholdUs) {
  this->lock.lock();

  #ifdef _DEBUG
  this->printContent();
  #endif

  struct timeval now;
  gettimeofday(&now, NULL);

  /// check the whole buffer

  bool somePacketTimeout = false;
  
  for (int i = 0; i < this->slideWindow.size(); i++) {
    SlideWindowSenderItem *item = this->slideWindow[i];

    #ifdef _DEBUG
    this->ensureCorrectness(item, i);
    #endif

    if (item->state == ITEM_STATE_SENT || item->state == ITEM_STATE_RESENT) {
      unsigned long elapsedUs = this->getElapsedUs(/*before=*/item->lastSentTime, /*after=*/now);
      if (elapsedUs > timeoutThresholdUs) {
        item->state = ITEM_STATE_TIMEOUT;
        somePacketTimeout = true;

        #ifdef _PRINT_WINDOW
        fprintf(stderr, "SlideWindowSender::checkTimeout timeout [%u]\n",
                item->seqNum);
        #endif
      }
    }
  }

  /// congestion control
  // if some data times out, the congestion window shrinks to 1
  // also avoids repeated punishment

  if ((!this->curCongestionWindowTimeouted) && somePacketTimeout) {
    this->curCongestionThreshold = (unsigned) (this->curCongestionWindow * CONGESTION_THRESHOLD_SHRINK);
    if (this->curCongestionThreshold < CONGESTION_THRESHOLD_MIN) {
      this->curCongestionThreshold = CONGESTION_THRESHOLD_MIN;
    }
    this->curCongestionWindow = CONGESTION_WINDOW_MIN;

    // set the new left bound
    this->congestionLeftBoundSeqNum = this->slideWindowLeftBoundSeqNum + this->curCongestionWindow;

    #ifdef _PRINT_WINDOW
    fprintf(stderr, "SlideWindowSender::checkTimeout congestion shrink to [%u] threshold [%u] leftBound [%u]\n",
            this->curCongestionWindow, this->curCongestionThreshold, this->congestionLeftBoundSeqNum);
    #endif

    // set the flag to avoid repeated punishment
    this->curCongestionWindowTimeouted = true;
  }

  /// unfortunately, our clever way of handling timeout does not work
  // there are too many corner cases, Ziang: I decided to do a simple traverse here

  if (!this->newDataReady) {
    for (int i = 0; i < this->slideWindow.size() && i < this->curCongestionWindow; i++) {
      SlideWindowSenderItem *item = this->slideWindow[i];
      if (item->state == ITEM_STATE_NOT_SENT || item->state == ITEM_STATE_TIMEOUT) {
        this->newDataReady = true;
        break;
      }
    }
  }

  this->lock.unlock();
}