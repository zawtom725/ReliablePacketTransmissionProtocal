/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * The sliding window protocal with the addition of:
 * 1. Buffer Control
 */

#include <mutex>
#include <map>
#include <deque>
#include <vector>
#include <sys/time.h>

#include "./packet.h"


#ifndef SLIDE_WINDOW_RECEIVER_H
#define SLIDE_WINDOW_RECEIVER_H


struct SlideWindowReceiverItem {
  unsigned int seqNum;

  // for state, see the macros in .cpp
  int state;
};


class SlideWindowReceiver {
public:
  SlideWindowReceiver(int fileToWriteFd);
  ~SlideWindowReceiver();

  // whether the transmission has finished or not
  bool finished();

  // when receiving a data packet
  AckPacket receiveDataPacket(DataPacket *dataPacket);


private:
  std::mutex lock;

  // file io
  int fileToWriteFd;

  // we will know the sequence number of the last packet from the sender
  bool lastPacketSeqNumReceived;
  unsigned int lastPacketSeqNum;

  // the sequence number of the next expected packet
  unsigned int nextExpectedPacketSeqNum;

  // a slide window of double ended queue with memory being dynamically managed
  // there must be at least curCongestionWindow number of items in the window
  unsigned int curWindowSize;
  std::deque<SlideWindowReceiverItem *> slideWindow;


private:
  // private helper that ensures the correctness of the slideWindow
  // it is added for debugging
  // TODO: once the protocal fully works, we can remove it
  void ensureCorrectness(SlideWindowReceiverItem *item, int slideWindowPosition);
};


#endif