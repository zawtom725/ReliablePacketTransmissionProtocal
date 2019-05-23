all: reliable_sender reliable_receiver

packet: ./src/packet.h ./src/packet.c
	g++ -c $?

slideWindowSender: ./src/slideWindowSender.h ./src/slideWindowSender.cpp
	g++ -c $?

slideWindowReceiver: ./src/slideWindowReceiver.h ./src/slideWindowReceiver.cpp
	g++ -c $?

rttEstimatorJacobson: ./src/rttEstimatorJacobson.h ./src/rttEstimatorJacobson.cpp
	g++ -c $?

reliable_sender: ./src/reliable_sender.cpp rttEstimatorJacobson slideWindowSender packet
	g++ -pthread $< -o $@ rttEstimatorJacobson.o slideWindowSender.o packet.o

reliable_receiver: ./src/reliable_receiver.cpp slideWindowReceiver packet
	g++ -pthread $< -o $@ slideWindowReceiver.o packet.o

clean:
	rm -f ./src/*.h.gch
	rm -f *.o
	rm -f reliable_receiver
	rm -f reliable_sender