## Reliable Packet Transmission Protocol
###### Author: Ziang Wan, Jiacheng Zhu
This repository implements a reliable communication protocol between two hosts on an unreliable link. The protocol uses a variant of [Sliding Window Protocol](https://en.wikipedia.org/wiki/Sliding_window_protocol) with [Congestion Control](https://en.wikipedia.org/wiki/TCP_congestion_control). The protocol can transmit data packets reliably and in-order. In a virtual testing environment, the protocol achieved a data throughput of 57 Mbps on a 100 Mbps link with average 7% IP packet loss rate.

### Set up a virtual testing environment
Assuming you have two hosts with IP 192.168.122.170 and 192.168.122.225. 
To check the bandwidth of the underlying link:
```
// at 192.168.122.225
ping 192.168.122.225
cat /dev/zero | nc 192.168.122.170 5201
// at 192.168.122.170
nc -l 5201 | pv -r
```
To check the loss rate and bandwidth of the underlying link:
```
// at 192.168.122.170
iperf3 -s
// at 192.168.122.225
iperf3 -c 192.168.122.170 -p 5201
```
Set the delay and loss rate for a virtual link consisted of two VMs on a single host. For details on how to set up the virtual link, see [KVM Networking](https://www.linux-kvm.org/page/Networking).
```
// set delay, loss rate and reorder rate
sudo modprobe sch_netem
sudo tc qdisc add dev ens3 root handle 1:0 netem delay 20ms loss 5% reorder 5%
// set bandwidth
sudo modprobe sch_tbf
sudo tc qdisc add dev ens3 parent 1:1 handle 10: tbf rate 100Mbit burst 40mb latency 25ms
```

### How to run the code
```
make
// at one host
./reliable_receiver UDPPort FileNameToWrite
// at the other host
./reliable_sender ReceiverHost ReceiverPort FileNameToRead BytesToSend
```
Note: Currently, the protocol sends a file over the network. However, it can be easily adapted to sending data packets. For details, see the source code functions `reliablyTransfer` and `reliablyReceive`.