# tcp-benchmark
Fork of directory https://github.com/yonch/fastpass/tree/master/src/tcp-benchmark by aousterh

TCP sender uses multiple sockets and select() to send to one/more TCP receivers (*short-lived-connections*).

Added a python wrapper around *tcp_sender.c* and *tcp_receiver.c* to
- run one/more senders and receivers on different CPUs using specific IP addresses/ ports for each.
- print the tail flow completion times for small, medium, large flows per receiver.

```
usage: run_tcp.py [-h] [--receiver_duration RECEIVER_DURATION]
                  [--sender_duration SENDER_DURATION]
                  [--inter_arrival_time INTER_ARRIVAL_TIME] [--size SIZE]
                  [--num_flows NUM_FLOWS] [--srcs SRCS [SRCS ...]]
                  [--dests DESTS [DESTS ...]] [--cdfFile CDFFILE]
                 [--scaling SCALING] [--percswitch_dir PERCSWITCH_DIR]

Run Tcp sender/ receiver

optional arguments:
  -h, --help            show this help message and exit
  --receiver_duration RECEIVER_DURATION
                        seconds
  --sender_duration SENDER_DURATION
                        seconds
  --inter_arrival_time INTER_ARRIVAL_TIME
                        microseconds
  --size SIZE           MTUs
  --num_flows NUM_FLOWS
                        senders send until num_flows are sent, or time runs
                        out, whichever comes first
  --srcs SRCS [SRCS ...]
                        list of senders, use ip:port notation
  --dests DESTS [DESTS ...]
                        list of receivers, use ip:port notation
  --cdfFile CDFFILE     not implemented yet
  --scaling SCALING     not implemented yet
  --percswitch_dir PERCSWITCH_DIR
                        not implemented yet
```


