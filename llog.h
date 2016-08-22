/*
 * llog.h
 *
 *  Created on: January 5, 2014
 *      Author: aousterh
 *      Modified: lav
 */

#ifndef LLOG_H_
#define LLOG_H_

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "common.h"
#define MAX_SENDERS 2
#define MAX_FLOW_ID 100000

// Information logger per flow

typedef struct flow_info {
  uint64_t send_time;
  uint64_t last_receive_time;
  uint64_t received_bytes;
  uint64_t total_bytes;
  uint64_t start_receive_time;
  uint64_t flow_start_time;
  uint64_t num_times_received;
} flow_info_t;


struct log {
  struct flow_info *flows_by_sender[MAX_SENDERS];
};

static inline
void init_log(struct log *log, uint32_t num_intervals) {
  assert(log != NULL);
  for (int i = 0; i < MAX_SENDERS; i++) {
    log->flows_by_sender[i] = NULL;
    log->flows_by_sender[i] = (struct flow_info *)
      calloc(MAX_FLOW_ID+1,
	     sizeof(struct flow_info));
    assert(log->flows_by_sender[i] != NULL);
  }
}

// Logs the arrival of a packet indicating the start of a flow
static inline void
log_flow_start(struct log *log, uint16_t node_id,
	       uint32_t flow_id,
	       uint32_t bytes,
	       uint32_t received_bytes,
	       uint64_t send_time,
	       uint64_t start_time,
	       uint64_t now) {
  // TODO(lav): add start time? flow id
  assert(log != NULL);
  assert(node_id < MAX_SENDERS);
  assert(flow_id <= MAX_FLOW_ID);
  assert(log->flows_by_sender[node_id] != NULL);
  flow_info_t* fi =
    &(log->flows_by_sender[node_id][flow_id]);
  fi->total_bytes = bytes;
  fi->received_bytes = received_bytes;
  fi->send_time = send_time;
  fi->last_receive_time = now;
  fi->start_receive_time = now;
  fi->flow_start_time = start_time;
  fi->num_times_received = 0;
  //printf("Flow %d of size %"PRIu64" started at %"PRIu64",",
  //	 flow_id, fi->total_bytes, start_time);
  
}

// Logs more bytes received
static inline
void log_data_received(struct log *log, uint16_t node_id,
		       uint32_t flow_id,
		       uint32_t bytes,
		       uint64_t now) {

  assert(log != NULL);
  assert(node_id < MAX_SENDERS);
  assert(flow_id <= MAX_FLOW_ID);
  struct flow_info *fi =
    &(log->flows_by_sender[node_id][flow_id]);
  fi->received_bytes += bytes;
  fi->last_receive_time = now;
  fi->num_times_received++;
}

// Logs flow completed
static inline
void log_flow_completed(struct log *log, uint16_t node_id,
			uint64_t fc_time) {
  assert(log != NULL);
  assert(node_id < MAX_SENDERS);
  // do nothing since we're already loggin all receives
}

// Prints the log contents in CSV format to stdout to be piped to a file
static inline
void write_out_log(struct log *log) {
  assert(log != NULL);
  printf("writing out log\n");
    for (int i = 0; i < MAX_SENDERS; i++) {
      struct flow_info* flows_from_sender = log->flows_by_sender[i];
      for (int flowId = 0; flowId < MAX_FLOW_ID; flowId++) {
	flow_info_t* fi = &(flows_from_sender[flowId]);
	if (fi->num_times_received == 0) continue;
	uint64_t start = fi->send_time;
	// when sender started sending after connection set up
	// fi->flow_start_time;  when flow officially started
	uint64_t fct = (fi->last_receive_time - start)*1e-3; // microseconds
	uint64_t id = (i * 10000 + flowId);
	uint64_t total_packets = ceil((1.0 * fi->total_bytes)/MTU_SIZE);
	uint64_t received_packets = ceil((1.0) * fi->received_bytes)/MTU_SIZE;
	printf("log: flow %u ended (queue 0) fct: %u size: %u acked: %u\n",
	       id, fct, total_packets, received_packets);
	  
	/* printf("%u, %"PRIu64", %"PRIu64", %"PRIu64"\n", */
	/*        flowId, fi->total_bytes, fi->received_bytes, */
	/*        fct); */
      }
    }
}

#endif /* LLOG_H_ */
