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

void tcp_sender_init(struct tcp_sender *sender, struct generator *gen,
		     uint32_t id, uint64_t duration, uint16_t port_num, const char *dest,
		     uint64_t num_flows) {
	sender->gen = gen;
	sender->id = id;
	sender->duration = duration;
	sender->port_num = port_num;
	sender->dest = dest;
	sender->num_flows = num_flows;
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
int open_socket_and_connect(struct tcp_sender *sender, int *sock_fd,
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

	// Initialize destination address
	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(sender->port_num);
	const char *ip_addr = sender->dest;
	// Choose the IP that corresponds to a randomly chosen core router
	//char ip_addr[12];
	//choose_IP(outgoing.receiver, ip_addr);
	//printf("chosen IP %s for receiver %d\n", ip_addr, outgoing.receiver);
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
	int ret = inet_pton(AF_INET, sender->dest, &outgoing.receiver);
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
		assert(time_diff < 1000 * 1000 * 1000);
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

			// Connect to the receiver
			connections[index].return_val = open_socket_and_connect(sender,
					&connections[index].sock_fd,
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
			int ret = inet_pton(AF_INET, sender->dest, &outgoing.receiver);
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
	uint32_t port_num = PORT;
	char *dest_ip = malloc(sizeof(char) * IP_ADDR_MAX_LENGTH);
	if (!dest_ip)
		return -1;
	uint32_t size_param;
	// short-lived/interactive (0), persistent/interactive (1), or persistent/bulk (2)

	uint32_t mean_t_btwn_flows = 10000;
	uint32_t num_flows = 10000;
	if (argc > 6) {
		sscanf(argv[1], "%u", &send_duration);
		sscanf(argv[2], "%u", &mean_t_btwn_flows);
		sscanf(argv[3], "%u", &my_id);
		sscanf(argv[4], "%s", dest_ip);
		sscanf(argv[5], "%u", &size_param);
		sscanf(argv[6], "%u", &num_flows);
		if (argc > 7)
			sscanf(argv[8], "%u", &port_num);
	}
	if (argc <= 6) {
		printf(
				"usage: %s send_duration mean_t my_id dest_ip size_param num_flows port_num (optional)\n",
				argv[0]);
		return -1;
	}

	uint64_t duration = (send_duration * 1ull) * 1000 * 1000 * 1000;

	mean_t_btwn_flows *= 1000; // get it in nanoseconds

	// Initialize the sender
	struct generator gen;
	struct tcp_sender sender;
	srand((uint32_t)(current_time_nanoseconds() + 0xDEADBEEF * my_id + 0xBABABABA * port_num));
	gen_init(&gen, POISSON, UNIFORM, mean_t_btwn_flows, size_param);
	tcp_sender_init(&sender, &gen, my_id, duration, port_num, dest_ip, num_flows);

	printf("Running interactive sender with short-lived connections\n");
	printf("\tmy id: %u\n", my_id);
	printf("\tduration (seconds): %u\n", send_duration);
	run_tcp_sender_short_lived(&sender);
}
