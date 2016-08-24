/*
 * tcp_receiver.c
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "common.h"
#include "llog.h"
#include "tcp_receiver.h" 
 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>
#include <stdbool.h>
#include <fcntl.h>

#define IP_ADDR_MAX_LENGTH 20
#define BITS_PER_BYTE 8
#define NUM_INTERVALS 10  // should be at least 1 interval per second!!!
#define MAX_BUFFER_SIZE (10000 * MTU_SIZE)

enum connection_state {
  INVALID,  // this socket is not currently in use
  ACCEPTING,  // accepting an incoming connection
  READING,  // reading in a flow
  WAITING   // waiting for a new flow on a persistent connection
};

struct receiving_connection {
  enum connection_state status;
  int sock_fd;
  uint64_t bytes_left;
  struct packet packet;
};

void tcp_receiver_init(struct tcp_receiver *receiver, uint64_t duration,
		       uint16_t port_num, const char* ip_addr)
{
  receiver->duration = duration;
  receiver->port_num = port_num;
  receiver->ip_addr = ip_addr;
  init_log(&receiver->log, NUM_INTERVALS);
}

// Create, bind, and listen to a socket using the port specified by receiver
// Returns the socket descriptor
int bind_and_listen_to_socket(struct tcp_receiver *receiver) {
  assert(receiver != NULL);

  struct sockaddr_in sock_addr;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(sock_fd > 0);
 
  // Initialize socket address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(receiver->port_num);
  if (receiver->ip_addr != NULL) {
    printf("Receiver IP address supplied %s\n", receiver->ip_addr);
    int  result = inet_pton(AF_INET, receiver->ip_addr,
			    &sock_addr.sin_addr);
    assert(result > 0);
  } else {
    sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } 
  // Bind the address to the socket
  int ret = bind(sock_fd,(struct sockaddr *)&sock_addr, sizeof(sock_addr));
  assert(ret != -1);

  if (receiver->ip_addr != NULL)
    printf("Bound receiver socket to %s:%d\n" ,
	   receiver->ip_addr, receiver->port_num);

  // Listen for incoming connections
  ret = listen(sock_fd, MAX_CONNECTIONS);
  assert(ret != -1);

  return sock_fd;
}


// Run a tcp_receiver with short-lived connections
void run_tcp_receiver_short_lived(struct tcp_receiver *receiver)
{
  printf("running tcp_receiver_short_lived.\n");
  assert(receiver != NULL);
  int i;
  struct sockaddr_in sock_addr;
  struct timespec timeout;

  timeout.tv_sec = 0;
  timeout.tv_nsec = 0;

  int sock_fd = bind_and_listen_to_socket(receiver);

  struct receiving_connection connections[MAX_CONNECTIONS];
  for (i = 0; i < MAX_CONNECTIONS; i++)
    connections[i].status = INVALID;

  fd_set rfds;
  FD_ZERO(&rfds);

  uint64_t start_time = current_time_nanoseconds();
  uint64_t end_time = start_time + receiver->duration;

  // Determine the next interval end time
  uint64_t interval_duration = receiver->duration / NUM_INTERVALS;
  uint64_t interval_end_time = start_time + interval_duration;
  //receiver->log.current->start_time = start_time;
  uint64_t time_now;
  char * buf = (char *) malloc(MAX_BUFFER_SIZE);
  while((time_now = current_time_nanoseconds()) < end_time + 1*1000*1000*1000uLL)
  {
    int i;

    // Add fds to set and compute max
    int max = sock_fd;
    FD_SET(sock_fd, &rfds);
    for (i = 0; i < MAX_CONNECTIONS; i++) {
      if (connections[i].status == INVALID)
	continue;

      FD_SET(connections[i].sock_fd, &rfds);
      if (connections[i].sock_fd > max)
	max = connections[i].sock_fd;
    }

    // Wait for a socket to have data to read or a new connection
    // or the end of this interval
    uint64_t time_diff;
    if (interval_end_time > time_now)
      time_diff = interval_end_time - time_now;
    assert(time_diff < 2 * 1000 * 1000 * 1000);
    timeout.tv_nsec = time_diff;

    int retval = pselect(max + 1, &rfds, NULL, NULL, &timeout, NULL);
    if (retval < 0)
      break;
    
    if (FD_ISSET(sock_fd, &rfds))
      {
	// Accept a new connection
	int accept_fd = accept(sock_fd, NULL, NULL);
	assert(accept_fd > 0);

	// Add to list of fds
	bool success = false;
	for (i = 0; i < MAX_CONNECTIONS; i++) {
	  if (connections[i].status == INVALID)
	    {
	      connections[i].sock_fd = accept_fd;
	      connections[i].status = ACCEPTING;
	      success = true;
	      break;
	    }
	}
	assert(success);  // Otherwise, we have too many connections
      }
      
    // Read from all ready connections
    for (i = 0; i < MAX_CONNECTIONS; i++)
      {
	if (connections[i].status == INVALID ||
	    !FD_ISSET(connections[i].sock_fd, &rfds))
	  continue;  // This fd is invalid or not ready

	int ready_fd = connections[i].sock_fd;
	int ready_index = i;

	if (connections[ready_index].status == ACCEPTING) {
	  // Read first part of flow
	  struct packet *incoming = &connections[ready_index].packet;
	  int bytes = read(ready_fd, incoming, sizeof(struct packet));
	  time_now = current_time_nanoseconds();
	  log_flow_start(&receiver->log,
			 incoming->sender,
			 incoming->id,
			 incoming->size * MTU_SIZE,
			 bytes,
			 incoming->packet_send_time,
			 incoming->flow_start_time,
			 time_now);
	  connections[ready_index].bytes_left = incoming->size * MTU_SIZE -
	    sizeof(struct packet);
	  connections[ready_index].status = READING;
	}
	else {
	  // Read in data
	  assert(connections[ready_index].status == READING);
	  int count = connections[ready_index].bytes_left < MAX_BUFFER_SIZE ?
	    connections[ready_index].bytes_left : MAX_BUFFER_SIZE;
	  int bytes = read(ready_fd, buf, count);
	  time_now = current_time_nanoseconds();
	  log_data_received(&receiver->log,
			    connections[ready_index].packet.sender,
			    connections[ready_index].packet.id,
			    bytes,
			    time_now);
	  connections[ready_index].bytes_left -= bytes;


	  if (connections[ready_index].bytes_left == 0)
	    {
	      // This flow is done!
	      struct packet *incoming = &connections[ready_index].packet;
	      /* printf("received,\t%d, %d, %d, %"PRIu64", %d, %"PRIu64"\n", */
	      /* 	     incoming->sender, incoming->receiver, incoming->size, */
	      /* 	     incoming->flow_start_time, incoming->id, time_now); */

	      if (incoming->flow_start_time < end_time) {
		log_flow_completed(&receiver->log, incoming->sender,
				   (time_now - incoming->flow_start_time));
	      }
 
	      int ret = shutdown(ready_fd, SHUT_RDWR);
	      assert(ret != -1);
	      close(ready_fd);
	  
	      // Remove from set
	      FD_CLR(ready_fd, &rfds);
	      connections[ready_index].status = INVALID;
	    }
	}
      }
    
    uint64_t now = current_time_nanoseconds();
    if (now > interval_end_time && now < end_time) {
      //receiver->log.current->end_time = now;

      // Start a new interval
      //receiver->log.current++;
      //receiver->log.current->start_time = now;
      //interval_end_time += interval_duration;
    }
  }

  close(sock_fd);
  free(buf);

  // Write log to a file
  write_out_log(&receiver->log);
}

int main(int argc, char **argv) {
  uint32_t receive_duration;
  uint32_t port_num = PORT;

  char *ip_addr = NULL;
  printf("Got %u arguments.\n", argc);
  if (argc > 1) {
    sscanf(argv[1], "%u", &receive_duration);
    if (argc > 2) {
      sscanf(argv[2], "%u", &port_num);  // optional port number
      printf("Got port number %u\n", port_num);
      if (argc > 3) {
	ip_addr = (char *) malloc(sizeof(char) * IP_ADDR_MAX_LENGTH);
	if (!ip_addr)
	  return -1;
	sscanf(argv[3], "%s", ip_addr);  // optional ip address
	printf("Got IP address %s\n", ip_addr);
      }
    }
  }
  
  if (argc <= 1) {
    printf("usage: %s receive_duration port_num (optional) ip (optional)\n",
	   argv[0]);
    return -1;
  }

  uint64_t duration = (receive_duration * 1ull) * 1000 * 1000 * 1000;

  // Initialize the receiver
  struct tcp_receiver receiver;
  
  tcp_receiver_init(&receiver, duration, port_num, ip_addr);
  run_tcp_receiver_short_lived(&receiver);
}
