/*
 * UIUC SP19 CS 438 MP3
 * Author: Ziang Wan
 * The main file for reliable sender.
 *
 * List of algorithms used in the sender:
 * 1. Sliding Window Protocal with Selective Repeat
 * 2. FIN - ACK one-way tear down
 * 3. RTT Estimation Jacobson's Algorithm
 * 4. TCP Slow Start
 * 5. *Fast Retransmission and Recovery
 *    Ziang: I feel like fast retransmission does not help much.
 * 
 * Ziang: I improve the time granularity to microseconds.
 */

#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <iostream>

#include <thread>
#include <mutex>
#include <condition_variable>

#include <vector>

#include "./packet.h"
#include "./rttEstimatorJacobson.h"
#include "./slideWindowSender.h"

using namespace std;


///////////////////////////////
/// defs
///////////////////////////////


// debug flags
// #define _PROFILING    1
// #define _PRINT_CONTROL  1
// #define _PRINT_PACKET   1
// #define _LOCAL_TEST     1

// socket timeout: 50 ms
#define SOCKET_TIMEOUT_US   (50 * 1000)

// rttEstimatorJacobson parameters
#define ESTIMATOR_ALPHA             0.125
#define ESTIMATOR_BETA              0.25

// slideWindow parameters
// fast retransmission also triggers fast recovery
#define SLIDE_WINDOW_FAST_RETRANSMISSION    true

// handshake
#define HANDSHAKE_NUM_AT_A_TIME   4
#define HANDSHAKE_DUPLICATE       3
#define HANDSHAKE_TIMEOUT_US      (60 * 1000)

// a parameter to control the frequency of timeout checker thread [0,1]
#define CHECK_TIMEOUT_INTERVAL_FRACTION   0.5

// number of parallel receiveAck threads
#define RECEIVE_ACK_THREAD_NUM  2


///////////////////////////////
/// globals
///////////////////////////////


// the sockets for reading/sending/receiving data
int fileToSendFd;
int transferSocket;

// the estimator
RttEstimatorJacobson *rttEstimator;

// the sliding window maintainer
SlideWindowSender *slideWindow;

// synchronizer
mutex condVarMutex;
condition_variable condVar;

// receiver address
struct sockaddr_in receiverAddr;

// for ending
bool done = false;

// profiling
#ifdef _PROFILING
unsigned int _profilingPacketsSent = 0;
unsigned long _profilingPacketSendingTotalTime = 0;
unsigned int _profilingAckReceived = 0;
unsigned long _profilingAckProcessingTotalTime = 0;
unsigned long _profilingTimeoutCheckTotalTime = 0;
#endif

///////////////////////////////
/// sokcet helper
///////////////////////////////


int createfileToSendFd(char *filePathToRead) {
  fileToSendFd = open(filePathToRead, O_RDONLY);
  if (fileToSendFd < 0) {
    perror("socket");
    return -1;
  }
  return fileToSendFd;
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

  #ifdef _LOCAL_TEST
  inet_pton(AF_INET, "127.0.0.1", &bindAddr.sin_addr);
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
/// Handshake
///////////////////////////////


unsigned long getElapsedUs(const struct timeval &before,
  const struct timeval &after) {
  unsigned long us = (after.tv_sec - before.tv_sec) * 1000 * 1000 +
                     (after.tv_usec - before.tv_usec);
  return us;
}


// return the estimate of RTT in microseconds
unsigned long performHandshake() {

  // only the TYPE_HANDSHAKE matters for the receiver
  DataPacket dataPacket;
  dataPacket.type = DATA_PACKET_TYPE_HANDSHAKE;
  dataPacket.seqNum = 0;
  dataPacket.dataSize = 0;
  // won't be used
  char placeholder[5];
  dataPacket.data = placeholder;  

  // for receiving the ACK of handshake
  AckPacket ackPacket;

  char handshakeBuffer[DATA_PACKET_SIZE];
  size_t bufferContentLength;

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_sender::performHandshake\n");
  #endif

  // take the average of three earliest received HANDSHAKE_ACK
  unsigned long initTimeoutUs[HANDSHAKE_DUPLICATE];
  int handshakeAckReceived = 0;
  struct timeval now;
  struct timeval now2;
  struct timeval h;

  // perform a two-way handshake until we determine the initial timeout in ms
  while (handshakeAckReceived < HANDSHAKE_DUPLICATE) {
    gettimeofday(&now, NULL);
    dataPacket.sentTime = now;

    bufferContentLength = fillBufferDataPacket(&dataPacket, handshakeBuffer);

    // send out handshake
    for (int i = 0; i < HANDSHAKE_NUM_AT_A_TIME; i++) {
      sendto(transferSocket, handshakeBuffer, bufferContentLength, 0,
        (struct sockaddr *) &receiverAddr, sizeof(struct sockaddr_in));
    }

    // waiting for handshake ack
    unsigned long timePassUs = 0;
    gettimeofday(&now, NULL);

    while (timePassUs < HANDSHAKE_TIMEOUT_US) {
      bufferContentLength = recvfrom(transferSocket, handshakeBuffer, DATA_PACKET_SIZE, 0,
        NULL, NULL);

      // fprintf(stderr, "getPacket [%ld]\n", bufferContentLength);

      if (bufferContentLength == ACK_PACKET_SIZE) {
        readBufferAckPacket(&ackPacket, handshakeBuffer, bufferContentLength);

        if (ackPacket.type == ACK_PACKET_TYPE_HANDSHAKE) {
          gettimeofday(&h, NULL);
          unsigned long elapsedUs = getElapsedUs(/*before=*/ackPacket.sentTime, /*after=*/h);

          #ifdef _PRINT_PACKET
          fprintf(stderr, "reliable_sender::performHandshake elapsedUs [%lu]\n",
                  elapsedUs);
          #endif

          initTimeoutUs[handshakeAckReceived] = elapsedUs;
          handshakeAckReceived += 1;

          if (handshakeAckReceived == HANDSHAKE_DUPLICATE) {
            // collect enough initial data
            break;
          }
        }
      }

      gettimeofday(&now2, NULL);
      timePassUs = getElapsedUs(/*before=*/now, /*after=*/now2);
    }
  }

  // the initTimeoutUs is the smallest of three and no greater than the hardcoded timeout
  unsigned long initTimeoutSmlstUs = initTimeoutUs[0];
  for (int i = 1; i < HANDSHAKE_DUPLICATE; i++) {
    if (initTimeoutUs[i] < initTimeoutSmlstUs) {
      initTimeoutSmlstUs = initTimeoutUs[i];
    }
  }
  if (HANDSHAKE_TIMEOUT_US < initTimeoutSmlstUs) {
    initTimeoutSmlstUs = HANDSHAKE_TIMEOUT_US;
  }

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_sender::performHandshake initTimeoutSmlstUs [%lu]\n",
          initTimeoutSmlstUs);
  #endif

  return initTimeoutSmlstUs;
}


///////////////////////////////
/// send data thread - multiplicity = 1
///////////////////////////////


void sendData() {
  unique_lock<mutex> sendDataLock(condVarMutex);

  char dataPacketBuffer[DATA_PACKET_SIZE];
  size_t bufferContentLength;

  #ifdef _PROFILING
  struct timeval before;
  struct timeval after;
  #endif

  while (!slideWindow->finished()) {
    // wait until there is data to send in the sliding window
    // or we are finished with data transmission
    condVar.wait(sendDataLock, []()->bool {
      bool slideWindowHasDatatoSend = slideWindow->hasDataToSend();
      bool finished = slideWindow->finished();
      return slideWindowHasDatatoSend || finished;
    });

    #ifdef _PROFILING
    gettimeofday(&before, NULL);
    #endif

    // consult the slideWindow to do new transmission and retransmission
    while (slideWindow->hasDataToSend()) {
      // the following function returns a vector of pointer on stack
      // each pointer points to a data packet to send on heap
      vector<DataPacket *> newPacketToSendVec = slideWindow->prepareDataPacketsToSend();

      #ifdef _PROFILING
      _profilingPacketsSent += newPacketToSendVec.size();
      #endif

      for (DataPacket *dp : newPacketToSendVec) {
        bufferContentLength = fillBufferDataPacket(dp, dataPacketBuffer);
        sendto(transferSocket, dataPacketBuffer, bufferContentLength, 0,
          (struct sockaddr *) &receiverAddr, sizeof(struct sockaddr_in));
        
        #ifdef _PRINT_PACKET
        fprintf(stderr, "reliable_sender::sendData: seq [%u] data bytes [%u]\n",
                dp->seqNum, dp->dataSize);
        #endif
      }

      // memory management
      for (DataPacket *dp : newPacketToSendVec) {
        delete dp;
      }
    }

    #ifdef _PROFILING
    gettimeofday(&after, NULL);
    _profilingPacketSendingTotalTime += getElapsedUs(before, after);
    #endif
  }

  sendDataLock.unlock();

  // if the sendData thread sees that all ACK, including the final FIN_ACK, has
  // been received, the transmissio is done.
  done = true;

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_sender::sendData: data sending completed\n");
  #endif
}


///////////////////////////////
/// receive ack thread - multiplicity = RECEIVE_ACK_THREAD_NUM
///////////////////////////////

// keep listening on transferSocket for incoming ACK or FIN until done
void receiveAck() {
  AckPacket ackPacket;
  char ackPacketBuffer[ACK_PACKET_SIZE];
  ssize_t bufferContentLength;

  #ifdef _PROFILING
  struct timeval before;
  struct timeval after;
  #endif

  while (!done) {
    memset(ackPacketBuffer, 0, ACK_PACKET_SIZE);
    bufferContentLength = recvfrom(transferSocket, ackPacketBuffer, ACK_PACKET_SIZE, 0, NULL, NULL);
    if (bufferContentLength != ACK_PACKET_SIZE) {
      continue;
    }

    #ifdef _PROFILING
    gettimeofday(&before, NULL);
    #endif

    readBufferAckPacket(&ackPacket, ackPacketBuffer, bufferContentLength);

    // received something other than data packet ack
    if (ackPacket.type != ACK_PACKET_TYPE_REGULAR) {
      continue;
    }

    #ifdef _PRINT_PACKET
    fprintf(stderr, "reliable_sender::receiveAck cseq [%u] sseq [%u]\n",
            ackPacket.cumulativeSeqNum, ackPacket.selectiveSeqNum);
    #endif

    // firstly, handles rttEstimator
    rttEstimator->ackArrived(&ackPacket);

    // then handles the slideWindow
    slideWindow->ackArrived(&ackPacket);
    if (slideWindow->hasDataToSend() || slideWindow->finished()) {
      condVar.notify_all();
    }

    #ifdef _PROFILING
    _profilingAckReceived += 1;
    gettimeofday(&after, NULL);
    _profilingAckProcessingTotalTime += getElapsedUs(before, after);
    #endif
  }

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_sender::receiveAck terminate\n");
  #endif
}


///////////////////////////////
/// check timeout thread - multiplicity = 1
///////////////////////////////


// periodically check timeout until the transmission has done
void checkTimeout() {

  #ifdef _PROFILING
  struct timeval before;
  struct timeval after;
  #endif

  while (!done) {

    #ifdef _PROFILING
    gettimeofday(&before, NULL);
    #endif

    long currentTimeoutThresholdUs = rttEstimator->getCurrentTimeoutUs();
    slideWindow->checkTimeout(currentTimeoutThresholdUs);
    if (slideWindow->hasDataToSend()) {
      condVar.notify_all();
    }
  
    #ifdef _PROFILING
    gettimeofday(&after, NULL);
    _profilingTimeoutCheckTotalTime += getElapsedUs(before, after);
    #endif

    // sleep according to the current timeout
    long timeToSleepUs = (long) (currentTimeoutThresholdUs * CHECK_TIMEOUT_INTERVAL_FRACTION);
    this_thread::sleep_for(chrono::microseconds(timeToSleepUs));
  }

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_sender::checkTimeout termiante\n");
  #endif
}


///////////////////////////////
/// the primary helper function
///////////////////////////////


int reliablyTransfer(char *rcvHostIP, unsigned short int rcvPort,
  char *filePathToRead, uint64_t bytesToSend) {
  int retCode;

  // prepare the receiver's address as a global
  memset(&receiverAddr, 0, sizeof(receiverAddr));
  receiverAddr.sin_family = AF_INET;
  receiverAddr.sin_port = htons(rcvPort);
  retCode = inet_pton(AF_INET, rcvHostIP, &(receiverAddr.sin_addr));
  if (retCode < 0) {
    perror("inet_pton");
    return -1;
  }

  // file fd
  retCode = createfileToSendFd(filePathToRead);
  if (retCode < 0) {
    return -1;
  }

  // NOTE: the socket is bind to INADDR_ANY:rcvPort
  retCode = createTransferSocket(rcvPort);
  if (retCode < 0) {
    close(fileToSendFd);
    return -1;
  }

  // perform a handshake to determine the initial rtt
  unsigned long initTimeoutMs = performHandshake();

  // bring on the core data structures
  rttEstimator = new RttEstimatorJacobson(initTimeoutMs,
                      ESTIMATOR_ALPHA, ESTIMATOR_BETA);

  slideWindow = new SlideWindowSender(fileToSendFd, bytesToSend,
                      SLIDE_WINDOW_FAST_RETRANSMISSION);

  // do work
  thread sendDataThread(sendData);
  thread receiveAckThreads[RECEIVE_ACK_THREAD_NUM];
  for (int i = 0; i < RECEIVE_ACK_THREAD_NUM; i++) {
    receiveAckThreads[i] = thread(receiveAck);
  }
  thread checkTimeoutThread(checkTimeout);

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_sender::All threads started\n");
  #endif

  // finish work
  sendDataThread.join();
  for (int i = 0; i < RECEIVE_ACK_THREAD_NUM; i++) {
    receiveAckThreads[i].join();
  }
  checkTimeoutThread.join();

  // free resource
  close(fileToSendFd);
  close(transferSocket);

  delete rttEstimator;
  delete slideWindow;

  #ifdef _PROFILING
  fprintf(stderr, "reliable_sender::profile packets [%u] sendTime [%lu] acks [%u] ackTime [%lu] checkTime [%lu]\n",
          _profilingPacketsSent, _profilingPacketSendingTotalTime, _profilingAckReceived, _profilingAckProcessingTotalTime,
          _profilingTimeoutCheckTotalTime);
  #endif

  #ifdef _PRINT_CONTROL
  fprintf(stderr, "reliable_sender::Transmission succeeds\n");
  #endif

  return 0;
}


///////////////////////////////
/// main
///////////////////////////////


int main(int argc, char *argv[]) {
  // parse input arguments
  if (argc != 5) {
    fprintf(stderr, "Usage: ./reliable_sender <rcv_hostname> <rcv_port> <file_path_to_read> <bytes_to_send>\n");
    return -1;
  }

  char *rcvHostIP = argv[1];
  unsigned short int rcvPort = (unsigned short int) atoi(argv[2]);
  char *filePathToRead = argv[3];
  uint64_t bytesToSend = atoll(argv[4]);

  // invoke the primary helper
  int retCode = reliablyTransfer(rcvHostIP, rcvPort, filePathToRead, bytesToSend);

  return retCode;
}