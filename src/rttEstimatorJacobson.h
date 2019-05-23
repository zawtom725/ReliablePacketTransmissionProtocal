/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * The main file for the rtt estimator using Jacobson's algorithm
 * 
 * Ziang: I improve the time granularity to microseconds.
 */
#include <mutex>
#include <map>

#include "./packet.h"


#ifndef RTT_ESTIMATOR_JACOBSON_H
#define RTT_ESTIMATOR_JACOBSON_H


class RttEstimatorJacobson {
public:
  RttEstimatorJacobson(unsigned long initTimeoutUs, double alpha, double beta);
  ~RttEstimatorJacobson();

  // use the sentTime field for a regular Ack
  void ackArrived(AckPacket *AckPacket);

  // get the current timeout
  unsigned long getCurrentTimeoutUs();


private:
  std::mutex lock;

  unsigned long curTimeoutUs;
  unsigned long curStdDevUs;

  // external passed parameters
  double alpha;
  double beta;


private:
  // get elapsed us - private helper
  unsigned long getElapsedUs(const struct timeval &before,
    const struct timeval &after) {
    unsigned long us = (after.tv_sec - before.tv_sec) * 1000 * 1000 +
                        (after.tv_usec - before.tv_usec);
    return us;
  }
};


#endif