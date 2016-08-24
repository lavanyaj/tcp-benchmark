# Macros
CC = g++
CCFLAGS = -g
#CCFLAGS += -DNDEBUG
#CCFLAGS += -O3
LDFLAGS = -lm -pthread -lrt

# Pattern rule
%.o: %.cc
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: tcp_receiver tcp_sender
clean:
	rm -f tcp_receiver tcp_sender *.o *~

# Dependency rules for file targets
tcp_receiver: tcp_receiver.o generate_packets.o ranvar.o llog.h
	$(CC) $< generate_packets.o ranvar.o -o $@ $(LDFLAGS)

tcp_sender: tcp_sender.o generate_packets.o ranvar.o llog.h
	$(CC) $< generate_packets.o ranvar.o -o $@ $(LDFLAGS)
