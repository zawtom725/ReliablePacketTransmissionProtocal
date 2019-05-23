/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * The main file for the rtt estimator using Jacobson's algorithm
 */
#include <math.h>
#include <sys/time.h>
#include <assert.h>

#include <iostream>

#include "./rttEstimatorJacobson.h"

using namespace std;


/////////////////////////
// Macros
/////////////////////////


// debug flags
//#define _PRINT_TIMING

// multiple the base curTimeoutUs by a ratio
// prevent the curStdDevUs getting too low and results in false timeouts
#define CUR_TIMEOUT_US_RATIO  1.1


/////////////////////////
// Implementation
/////////////////////////


RttEstimatorJacobson::RttEstimatorJacobson(unsigned long initTimeoutUs,
  double alpha, double beta) {
  this->curTimeoutUs = initTimeoutUs;
  this->curStdDevUs = (unsigned long) (initTimeoutUs / 4);
  this->alpha = alpha;
  this->beta = beta;
}


RttEstimatorJacobson::~RttEstimatorJacobson() {
  // nothing, no data on heap
}


void RttEstimatorJacobson::ackArrived(AckPacket *ackPacket) {
  this->lock.lock();

  // not handshake
  assert(ackPacket->type == ACK_PACKET_TYPE_REGULAR);

  struct timeval now;
  gettimeofday(&now, NULL);

  // update recorded RTT and StdDev
  unsigned long elapsedUs = this->getElapsedUs(/*before=*/ackPacket->sentTime, /*after=*/now);

  this->curStdDevUs = (unsigned long)
    (this->beta * (elapsedUs >= this->curTimeoutUs ? elapsedUs - this->curTimeoutUs : this->curTimeoutUs - elapsedUs) +
    (1 - this->beta) * this->curStdDevUs);

  this->curTimeoutUs = (unsigned long)
    (this->alpha * elapsedUs +
    (1 - this->alpha) * this->curTimeoutUs);

  #ifdef _PRINT_TIMING
  fprintf(stderr, "RttEstimatorJacobson::ackArrived elapsed [%lu] update timeoutUs [%lu] stddev [%lu]\n",
          elapsedUs, this->curTimeoutUs, this->curStdDevUs);
  #endif

  this->lock.unlock();
}


unsigned long RttEstimatorJacobson::getCurrentTimeoutUs() {
  this->lock.lock();
  unsigned long retTimeoutUs = this->curTimeoutUs * CUR_TIMEOUT_US_RATIO +
                               4 * this->curStdDevUs;
  this->lock.unlock();

  #ifdef _PRINT_TIMING
  fprintf(stderr, "RttEstimatorJacobson::getCurrentTimeoutMs curTimeout: [%lu]\n",
          retTimeoutUs);
  #endif

  return retTimeoutUs;
}
