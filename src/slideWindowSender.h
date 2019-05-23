/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * The sliding window protocal with the addition of:
 * 1. Selective Repeat with both cumulative and selective acks
 * 2. TCP Slow Start, Congestion Control
 * 3. Fast Recovery and Fast Retransmission
 * 4. Buffer Control
 */

#include <mutex>
#include <map>
#include <deque>
#include <vector>
#include <sys/time.h>

#include "./packet.h"


#ifndef SLIDE_WINDOW_SENDER_H
#define SLIDE_WINDOW_SENDER_H


struct SlideWindowSenderItem {
  unsigned int seqNum;

  // for state, see the macros in .cpp
  int state;
  
  // when the data packet corresponding to this item is sent out
  // the lastSentTime will keep track of its time out process
  struct timeval lastSentTime;
  
  // dataSize usually is DATA_PACKET_DATA_SIZE but the last item could be shorter
  int dataSize;

  // a temporary buffer of data from file
  char data[DATA_PACKET_DATA_SIZE];
};


class SlideWindowSender {
public:
  SlideWindowSender(int fileToSendFd,
    uint64_t bytesToSend, bool fastRetransmission);
  ~SlideWindowSender();

  // whether the transmission has finished or not
  bool finished();

  // whether the protocal needs to send data or not
  bool hasDataToSend();

  // prepare the next data packets to send and then assume they are send
  // the returned data packets are on heap <- delete/free is needed.
  std::vector<DataPacket *> prepareDataPacketsToSend();

  // when an ack has arrived
  void ackArrived(AckPacket *ackPacket);

  // the following will be periodically invoked to check timeout
  void checkTimeout(unsigned long timeoutThresholdMs);


private:
  std::mutex lock;
  std::map<unsigned int, struct timeval> timerMap;

  // file io
  int fileToSendFd;

  // track when it is finished
  // assume that each segment is of DATA_PACKET_DATA_SIZE long
  uint64_t bytesToSend;
  uint64_t bytesSent;

  // just for convenience
  unsigned int lastPacketSeqNum;
  int lastPacketSize;

  // fast retransmission
  bool fastRetransmission;
  unsigned int lastAck;
  int lastAckAckedCount;

  // NOTE: Ziang: from my experiment, I observe that the congestion window gets
  // continuously punished because of a mixture and timeout and fast retransmission
  // therefore, I use the following flag to make sure the congestion window only
  // gets punished once for each round of sending.
  bool curCongestionWindowTimeouted;

  // NOTE: If we are using unsigned int as our sequence number
  // We can utilize its automatic wrap around.

  // the flag that indicates whether there is new data ready in the deque
  bool newDataReady;

  // SlideWindowLeftBound is the left most UN-ACKED packet.
  unsigned int slideWindowLeftBoundSeqNum;

  // a slide window of double ended queue with memory being dynamically managed
  // there must be at least curCongestionWindow number of items in the window
  std::deque<SlideWindowSenderItem *> slideWindow;

  // NOTE: we choose not to implement advertised window. Therefore,
  // congestion control is the only thing that matters on the sender's side.
  unsigned int curCongestionWindow;
  unsigned int curCongestionThreshold;

  // if slideWindowleftBoundSeqNum >= congestionLeftBoundSeqNum,
  // the congestion window grows
  // the new congestionLeftBoundSeqNum = old congestionLeftBoundSeqNum + new curCongestionWindow
  unsigned int congestionLeftBoundSeqNum;


private:
  // the private helper that reads a chunk of data at position of size seqNum * DATA_PACKET_DATA_SIZE
  // might read less data when approach the end of the file
  void readFile(SlideWindowSenderItem *item, unsigned int seqNum, int size);

  // private helper that ensures the correctness of the slideWindow
  // it is added for debugging
  // TODO: once the protocal fully works, we can remove it
  void ensureCorrectness(SlideWindowSenderItem *item, int slideWindowPosition);

  // print the current content of the slideWindow
  void printContent();

  // get elapsed us - private helper
  unsigned long getElapsedUs(const struct timeval &before,
    const struct timeval &after) {
    unsigned long us = (after.tv_sec - before.tv_sec) * 1000 * 1000 +
                       (after.tv_usec - before.tv_usec);
    return us;
  }
};


#endif