/*
 * tcp_sender.c
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "common.h"
#include "generate_packets.h"
#include "tcp_sender.h" 

#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>

#define IP_ADDR_MAX_LENGTH 20
#define MAX_BUFFER_SIZE (10000 * MTU_SIZE)
#define NUM_CORES 4

enum state {
	INVALID, CONNECTING, SENDING, READY
};

struct connection {
	enum state status;
	int sock_fd;
	int return_val;
	uint64_t bytes_left;
	char *buffer;
	char *current_buffer;
};

// receiver_alias is just an empty char array
static void get_alias(const char* sender_ip, const char* receiver_ip, char* receiver_alias) {
  strcpy(receiver_alias,"");
  if (strncmp(sender_ip, "10.50.0.1", IP_ADDR_MAX_LENGTH) == 0) {
    if (strncmp(receiver_ip, "10.50.1.1", IP_ADDR_MAX_LENGTH) == 0)
      strcpy(receiver_alias, "10.60.1.1");
  }
}

void tcp_sender_init(struct tcp_sender *sender, struct generator *gen,
		     uint32_t id, uint64_t duration, uint64_t num_flows,
		     int num_dests, char **dest_arr,  uint32_t* port_num_arr, const char* src_ip, uint32_t src_port, char **alias_arr) {
	sender->gen = gen;
	sender->id = id;
	sender->duration = duration;
	sender->num_dests = num_dests;
	assert(num_dests <= NUM_CORES);

	for (int i = 0; i < NUM_CORES; i++) {
	  sender->port_num_arr[i] = port_num_arr[i];
	  sender->dest_arr[i] = dest_arr[i];
	  sender->alias_arr[i] = alias_arr[i];
	}
	
	for (int i = 0; i < NUM_CORES; i++) {
	  if (i < num_dests) {
	    printf("%d) %s:%u (%s), ", i, sender->dest_arr[i], sender->port_num_arr[i], sender->alias_arr[i]);
	  } else {
	    assert(sender->dest_arr[i] == NULL);
	    assert(sender->port_num_arr[i] == 0);
	  }
	}
	printf("\nsuccessfully initialized sender\n");
	sender->num_flows = num_flows;
	sender->src_port = src_port;
	sender->src_ip = src_ip;
}

int choose_random_dest_index(const struct tcp_sender* sender) {
  assert(sender->num_dests > 0);
  return rand() / ((double) RAND_MAX) * (sender->num_dests);
}

// Selects the IP that corresponds to the receiver id
// Note that this only works for the original 8-machine testbed topology
// Will need to be rewritten for different topologies
void choose_IP(uint32_t receiver_id, char *ip_addr) {
	int index = 0;
	int core = rand() / ((double) RAND_MAX) * NUM_CORES + 1;
	switch (core) {
	case (1):
		ip_addr[index++] = '1';
		break;
	case (2):
		ip_addr[index++] = '2';
		break;
	case (3):
		ip_addr[index++] = '3';
		break;
	case (4):
		ip_addr[index++] = '4';
		break;
	default:
		assert(0);
	}

	ip_addr[index++] = '.';
	ip_addr[index++] = '1';
	ip_addr[index++] = '.';

	switch (receiver_id >> 1) {
	case (0):
		ip_addr[index++] = '1';
		break;
	case (1):
		ip_addr[index++] = '2';
		break;
	case (2):
		ip_addr[index++] = '3';
		break;
	case (3):
		ip_addr[index++] = '4';
		break;
	default:
		assert(0);
	}

	ip_addr[index++] = '.';

	switch (receiver_id % 2) {
	case (0):
		ip_addr[index++] = '1';
		break;
	case (1):
		ip_addr[index++] = '2';
		break;
	default:
		assert(0);
	}

	ip_addr[index++] = '\0';
}

// Open a socket and connect (dst specified by sender).
// Populate sock_fd with the descriptor and return the return value for connect
int open_socket_and_connect(struct tcp_sender *sender, int *sock_fd, int dest_index,
bool non_blocking) {
	assert(sender != NULL);
	
	struct sockaddr_in sock_addr;
	int result;

	// Create a socket, set to non-blocking
	*sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	assert(*sock_fd != -1);
	if (non_blocking) {
		result = fcntl(*sock_fd, F_SETFL, O_NONBLOCK);
		assert(result != -1);
	}

	if (sender->src_ip != NULL && sender->src_port != 0) {
	  // bind socket to given IP address
	  // Initialize socket address
	  memset(&sock_addr, 0, sizeof(sock_addr));
	  sock_addr.sin_family = AF_INET;
	  sock_addr.sin_port = 0; //htons(sender->src_port+index);
	  int  result = inet_pton(AF_INET, sender->src_ip, &sock_addr.sin_addr);
	  assert(result > 0);
	  result =
	    bind(*sock_fd,(struct sockaddr *)&sock_addr, sizeof(sock_addr));
	  assert(result != -1);
	  printf("Bound source socket to %s:random_port\n",
		 sender->src_ip);
	}
	
	// Initialize destination address
	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	const char *ip_addr = sender->alias_arr[dest_index];	
	uint16_t port_num = sender->port_num_arr[dest_index];
	printf("connecting to %s:%u\n", ip_addr, port_num);
	sock_addr.sin_port = htons(port_num);
	result = inet_pton(AF_INET, ip_addr, &sock_addr.sin_addr);
	assert(result > 0);


	// Connect to the receiver
	return connect(*sock_fd, (struct sockaddr *) &sock_addr, sizeof(sock_addr));
}
// Run a tcp_sender with short-lived connections
void run_tcp_sender_short_lived(struct tcp_sender *sender) {
	assert(sender != NULL);
	struct packet outgoing;
	struct gen_packet packet;
	int count = 0;
	int i;
	struct timespec ts;
	uint32_t flows_sent = 0;

	ts.tv_sec = 0;

	uint64_t start_time = current_time_nanoseconds();
	uint64_t end_time = start_time + sender->duration;
	uint64_t next_send_time;

	// Info about connections
	struct connection connections[MAX_CONNECTIONS];
	for (i = 0; i < MAX_CONNECTIONS; i++)
		connections[i].status = INVALID;

	fd_set wfds;
	FD_ZERO(&wfds);

	// Generate the first outgoing packet
	gen_next_packet(sender->gen, &packet);
	next_send_time = start_time + packet.time;

	outgoing.sender = sender->id;
	outgoing.dest_index = choose_random_dest_index(sender);
	int ret = inet_pton(AF_INET, sender->dest_arr[outgoing.dest_index], &outgoing.receiver);
	assert(ret > 0);
	outgoing.flow_start_time = next_send_time;
	outgoing.size = packet.size;
	outgoing.id = count++;

	while (current_time_nanoseconds() < end_time) {
		// Set timeout for when next packet should be sent
		uint64_t time_now = current_time_nanoseconds();
		uint64_t time_diff = 0;
		if (next_send_time > time_now)
			time_diff = (next_send_time < end_time ? next_send_time : end_time)
					- time_now;
		assert(time_diff < 2 * 1000 * 1000 * 1000);
		ts.tv_nsec = time_diff;

		// Add fds to set and compute max
		FD_ZERO(&wfds);
		int max = 0;
		for (i = 0; i < MAX_CONNECTIONS; i++) {
			if (connections[i].status == INVALID)
				continue;

			FD_SET(connections[i].sock_fd, &wfds);
			if (connections[i].sock_fd > max)
				max = connections[i].sock_fd;
		}

		// Wait for a socket to be ready or for it to be time to start a new connection
		int retval = pselect(max + 1, NULL, &wfds, NULL, &ts, NULL);
		if (retval < 0)
			break;

		if ((current_time_nanoseconds() >= next_send_time)
		    && (outgoing.id < sender->num_flows)) {
			// Open a new connection

			// Find an index to use
			int index = -1;
			for (i = 0; i < MAX_CONNECTIONS; i++) {
				if (connections[i].status == INVALID) {
					connections[i].status = CONNECTING;
					index = i;
					break;
				}
			}
			assert(index != -1);  // Otherwise, we have too many connections

			// Connect to the receiver, receiver must match outgoing
			connections[index].return_val =
			  open_socket_and_connect(sender,
						  &connections[index].sock_fd,
						  outgoing.dest_index,
						  true);
			if (connections[index].return_val < 0 && errno != EINPROGRESS) {
				assert(current_time_nanoseconds() > end_time);
				return;
			} else {	
			  //printf("opened socket # %d to %s and connected non-blocking\n", index, sender->dest);	
			}

			int size_in_bytes = outgoing.size * MTU_SIZE;
			connections[index].buffer = malloc(size_in_bytes);
			bcopy((void *) &outgoing, connections[index].buffer,
					sizeof(struct packet));

			// Generate the next outgoing packet
			gen_next_packet(sender->gen, &packet);
			next_send_time = start_time + packet.time;

			outgoing.sender = sender->id;
			outgoing.dest_index = choose_random_dest_index(sender);
			printf("index, dest for next packet is %d %s:%d\n", \
			       outgoing.dest_index, sender->dest_arr[outgoing.dest_index],
			       sender->port_num_arr[outgoing.dest_index]);

			int ret = inet_pton(AF_INET, sender->dest_arr[outgoing.dest_index],
					    &outgoing.receiver);
			assert(ret > 0);
			outgoing.flow_start_time = next_send_time;
			outgoing.size = packet.size;
			outgoing.id = count++;
		}

		// Handle all existing connections that are ready
		for (i = 0; i < MAX_CONNECTIONS; i++) {
			int result;
			socklen_t result_len = sizeof(result);

			if (connections[i].status
					== INVALID|| !FD_ISSET(connections[i].sock_fd, &wfds))
				continue;  // This fd is invalid or not ready

			if (connections[i].status == CONNECTING) {
				// check that connect was successful
				int ret = getsockopt(connections[i].sock_fd, SOL_SOCKET,
						SO_ERROR, &result, &result_len);
				assert(ret == 0);
				assert(result == 0);

				// send data
				struct packet *outgoing_data =
						(struct packet *) connections[i].buffer;
				connections[i].bytes_left = outgoing_data->size * MTU_SIZE;
				outgoing_data->packet_send_time = current_time_nanoseconds();
				connections[i].return_val = send(connections[i].sock_fd,
						connections[i].buffer, connections[i].bytes_left, 0);
				connections[i].status = SENDING;
				if (DEBUG_PRINTS)
					printf("sent, \t\t%d, %d, %d, %"PRIu64", %d, %"PRIu64"\n",
							outgoing_data->sender, outgoing_data->receiver,
							outgoing_data->size, outgoing_data->flow_start_time,
							outgoing_data->id, current_time_nanoseconds());
				flows_sent++;
			} else if (connections[i].status == SENDING
				   && connections[i].return_val < connections[i].bytes_left) {
			  // did not finish sending this flow, send some more
			  // don't want to send all at once to be fair to other flows
			  // not changing packet_send_time since that's start time use for flow fct
			  connections[i].bytes_left -= connections[i].return_val;
			  connections[i].return_val = send(connections[i].sock_fd,
							   connections[i].buffer, connections[i].bytes_left, 0);
			  connections[i].status = SENDING;
			  if (DEBUG_PRINTS) {
			    struct packet *outgoing_data =
			      (struct packet *) connections[i].buffer;
			    printf("sent, \t\t%d, %d, %d, %"PRIu64", %d, %"PRIu64"\n",
				   outgoing_data->sender, outgoing_data->receiver,
				   outgoing_data->size, outgoing_data->flow_start_time,
				   outgoing_data->id, current_time_nanoseconds());
			  }
			} else {
				// check that send was succsessful
				// if this assert fails, can implement the method for persistent
				// connections above to resend the remainder. but this should only
				// be a problem with really large flow sizes.
			        assert(connections[i].return_val == connections[i].bytes_left);

				// close socket
				(void) shutdown(connections[i].sock_fd, SHUT_RDWR);
				close(connections[i].sock_fd);

				// clean up state
				free(connections[i].buffer);
				connections[i].status = INVALID;
			}
		}
	}

	printf("sent %d flows\n", flows_sent);

}

int main(int argc, char **argv) {
	uint32_t send_duration;  // in seconds
	uint32_t my_id;

	uint32_t size_param;
	uint32_t mean_t_btwn_flows = 10000;
	uint32_t num_flows = 10000;

	char *dest_arr[NUM_CORES];
	char *alias_arr[NUM_CORES];
	uint32_t port_num_arr[NUM_CORES];
	int num_dests = 0;
	for (int i = 0; i < NUM_CORES; i++) {
	  dest_arr[i] = NULL;
	  alias_arr[i] = NULL;
	  port_num_arr[i] = 0;
	}

	char *src_ip = NULL;	
	uint32_t src_port = 0;
	const char* expected[] = {"binary", "id_", "send_duration", "mean_t_btwn_flows",
				   "size_param", "num_flows", "num_dests",
				  "dest:port", "src:port", "...", "...", "..."};
	for (int i = 0; i < argc; i++) {
	  printf("arg #%d: expected %s, got %s\n", i, expected[i], argv[i]);
	}
	if (argc > 7) {
	  sscanf(argv[1], "%u", &my_id);       
	  sscanf(argv[2], "%u", &send_duration);
	  sscanf(argv[3], "%u", &mean_t_btwn_flows);	  
	  sscanf(argv[4], "%u", &size_param);
	  sscanf(argv[5], "%u", &num_flows);

	  sscanf(argv[6], "%u", &num_dests);
	  int index = 0;
	  int next_index = 7;
	  while (index < num_dests) {
	    dest_arr[index] = malloc(sizeof(char) * IP_ADDR_MAX_LENGTH);
	    if (!dest_arr[index]) return -1;		  
	    sscanf(argv[next_index], "%[^:]:%d", dest_arr[index], &port_num_arr[index]);
	    /* printf("scanned %s into %s and %u\n", */
	    /* 	   argv[next_index], dest_arr[index], port_num_arr[index]); */
	    index++; next_index++;
	  }
	  if (argc > next_index) {
	    src_ip = malloc(sizeof(char) * IP_ADDR_MAX_LENGTH);
	    if (!src_ip) return -1;		  
	    sscanf(argv[next_index], "%[^:]:%u", src_ip, &src_port);
	  }		  		
	}

	if (src_ip) {
	  for (int i = 0; i < num_dests; i++) {
	    // get the alias for this destination
	    alias_arr[i] = malloc(sizeof(char) * IP_ADDR_MAX_LENGTH);
	    if (!alias_arr[i]) return -1;
	    get_alias(src_ip, dest_arr[i], alias_arr[i]);
	    assert(strcmp(alias_arr[i], "") != 0);
	    printf("got source %s will send to alias %s instead of %s\n",
		   src_ip, alias_arr[i], dest_arr[i]);
	  }
	}

	if (argc <= 7) {
		printf(
		       "usage: %s send_duration_s mean_t_us my_id_0 size_param_mtu num_flows num_dests dest_ip:port_num+ src_ip:port\n",
		       argv[0]);
		return -1;
	}

	uint64_t duration = (send_duration * 1ull) * 1000 * 1000 * 1000;
	mean_t_btwn_flows *= 1000; // get it in nanoseconds

	// Initialize the sender
	struct generator gen;
	struct tcp_sender sender;
	srand((uint32_t)(current_time_nanoseconds() + 0xDEADBEEF * my_id + 0xBABABABA * src_port));
	gen_init(&gen, POISSON, ONE_SIZE, mean_t_btwn_flows, size_param);
	tcp_sender_init(&sender, &gen, my_id, duration, num_flows,
			num_dests, dest_arr, port_num_arr, src_ip, src_port, alias_arr);
	
	printf("Running interactive sender with short-lived connections\n");
	printf("\tmy id: %u\n", my_id);
	printf("\tduration (seconds): %u\n", send_duration);
	run_tcp_sender_short_lived(&sender);
}
