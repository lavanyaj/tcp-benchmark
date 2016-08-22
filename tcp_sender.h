/*
 * tcp_sender.h
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "generate_packets.h"

#include <stdint.h>

#ifndef TCP_SENDER_H_
#define TCP_SENDER_H_

#define NUM_CORES 4

struct tcp_sender {
  struct generator *gen;
  uint32_t id;
  uint64_t duration;  // How long to send for
  uint16_t port_num_arr[NUM_CORES];
  uint64_t num_flows; // Number of flows to send
  char *dest_arr[NUM_CORES];
  int num_dests;
  uint16_t src_port;
  const char *src_ip;
};

// Inits a tcp sender.
void tcp_sender_init(struct tcp_sender *sender, struct generator *gen, uint32_t id,
		     uint64_t duration, uint64_t num_flows, 
		     int num_dests, char **dest_arr, uint32_t* port_num_arr,
		     const char* src_ip,  uint32_t src_port);

// Runs one TCP sender with a persistent connection
void run_tcp_sender_persistent(struct tcp_sender *sender);

// Runs one TCP sender with short-lived connections
void run_tcp_sender_short_lived(struct tcp_sender *sender);

#endif /* TCP_SENDER_H_ */
