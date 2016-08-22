from tempfile import NamedTemporaryFile
from multiprocessing import Pool
import multiprocessing
import subprocess
import argparse
import os
import sys
import time

parser = argparse.ArgumentParser(description='Run Tcp sender/ receiver')

parser.add_argument('--receiver_duration', type=int, help='seconds', default=10)
parser.add_argument('--sender_duration', type=int, help='seconds', default=5)
parser.add_argument('--inter_arrival_time', help='microseconds', type=int, default=10000)
parser.add_argument('--size', type=int, help='MTUs', default=100)
parser.add_argument('--num_flows', type=int, default=1000)

parser.add_argument('--rtt', type=float, default=0)
parser.add_argument('--tcp_benchmark_dir', type=str, default="/home/lavanyaj/tcp-benchmark")
parser.add_argument('--percswitch_dir', type=str, default="/home/lavanyaj/perc_switch")

# single sender and receiver set up
parser.add_argument('--srcs', type=str, nargs='+', default=["10.0.0.1:500", "10.0.0.2:500"])
parser.add_argument('--dests', type=str, nargs='+', default=["10.0.0.3:100", "10.0.0.4:100"])

# not used yet
parser.add_argument('--scaling', type=float, default=1.0)

args = parser.parse_args()


def get_sender_cmds():
    tcp_sender_cmds = {}
    tcpSender = "%s/tcp_sender"%args.tcp_benchmark_dir
    id_ = 0
    for addr in args.srcs:
        ip, port = addr.split(":")
        # sudo /home/lavanyaj/tcp-benchmark/tcp_sender 5 10000 0 10.0.0.2 10 100 100 500 10.0.0.1
        sender_cmd = tcpSender +\
                     " " + str(id_) +\
                     " " + str(args.sender_duration) +\
                     " " + str(args.inter_arrival_time) +\
                     " " + str(args.size) +\
                     " " + str(args.num_flows) +\
                     " " + str(len(args.dests)) +\
                     " " + " ".join(args.dests)+\
                     " " + ip + ":" + port
        tcp_sender_cmds[ip] = ("sender", ip, sender_cmd)
        id_ = id_ + 1

    return tcp_sender_cmds

def get_receiver_cmds():
    tcp_receiver_cmds = {}
    tcpReceiver = "%s/tcp_receiver"%args.tcp_benchmark_dir
    for addr in args.dests:
        ip, port = addr.split(":")
        receiver_cmd = tcpReceiver + " " + str(args.receiver_duration) +\
                       " " + port +\
                       " " + ip
        tcp_receiver_cmds[ip] = ("receiver", ip, receiver_cmd)
    return tcp_receiver_cmds

def get_stats_cmds():
    filters = {}
    filters["small"] = "$10 * 1500 < 1e4"
    filters["medium"] = "$10 * 1500 >= 1e4 && $10 * 1500 < 1e6"
    filters["large"] = "$10 * 1500 >= 1e6"

    grep_fct = "grep fct"
    print_fcts = lambda f: "awk '{ if (%s) {print $8 \" \" $10*1.2;}}'"%f
    normalize = "awk '{print $1/$2;}'"
    r_tail = "Rscript -e 'quantile (as.numeric (readLines (\"stdin\")),"\
             + " probs=c(0.5, 0.95))' | tail -n 1 | awk '{print $2;}'"
    stats_cmds = {}
    for size in filters:
        cmd = "| ".join([grep_fct, print_fcts(filters[size]),\
                         normalize, r_tail])
        stats_cmds[size] = cmd
    return stats_cmds

def run_tcp(cmd):
    (kind, ip, cmd) = cmd
    prefix = "%s_%s_"%(kind, ip)
    f = NamedTemporaryFile(prefix=prefix,delete=False)
    e = NamedTemporaryFile(delete=False)
    ret = subprocess.call(cmd.split(), stdout=f, stderr=e)
    return "%d %s %s" % (ret, f.name, e.name)

def run_stats(open_file, cmd):
    # print("running %s on %s" % (cmd, input_file))
    # for line in input_file:
    #    print line

    err = ""
    out = ""
    try:
        out = subprocess.check_output(cmd,
                                      stdin = open_file,
                                      shell = True)
    except subprocess.CalledProcessError as e:
        err = e.output
        out = ""
    
    return (out, err)
    #return (cmd, " ")

def run_tcp_processes(tcp_cmds):
    # want to run TCP senders and receivers on different cores
    count = multiprocessing.cpu_count()
    assert(len(tcp_cmds) + 1 < count)
    count = len(tcp_cmds)
    pool = Pool(processes=count)
    results = pool.map(run_tcp, tcp_cmds)
    pool.close()
    pool.join()

    # look for errors
    i = 0
    for res in results:
        # TODO(lav): sometimes no error but output file has no results
        if (res.split()[0] < 0):
            print("error running tcp command %s" % tcp_cmds[i])
            subprocess.call(["cat", res.split()[2]])
            print("error running tcp command %s" % tcp_cmds[i])
            return None

    return results

def run_stats_processes(receiver_outputs, stats_cmds):
    stats_outputs = []
    for output in receiver_outputs:
        kind, fName, eName = output.split()
        fPrefix = "_".join(fName.split("_")[:2])
        f = open(fName, "r")
        for name in stats_cmds:
            cmd = stats_cmds[name]
            out, err = run_stats(f, cmd)
            out_str = "%s %s %s" % (fPrefix, name, out)
            if len(err) > 0:
                out_str = "%s %s %s" % (fPrefix, name, err)
            stats_outputs.append(out_str)
        f.close()        
    return stats_outputs



# def get_sender_cmds(ip_addr_list):
# def get_receiver_cmds(ip_addr_list):
# def get_stats_cmds():
# def run_tcp(cmd):
# def run_stats(proc, input_file, input_err, cmd)
# def run_tcp_processes(tcp_cmds):
# def run_stats_processes(tcp_cmds, tcpCmdOutputs, stats_cmds):

if __name__ == '__main__':
    assert(len(args.srcs) <= 2)
    assert(len(args.srcs) >= 1)
    assert(len(args.dests) <= 2)
    assert(len(args.dests) >= 1)
    
    tcp_sender_cmds = get_sender_cmds()
    print("getting tcp sender commands: %s\n"%str(tcp_sender_cmds))
    tcp_receiver_cmds = get_receiver_cmds()
    print("getting tcp receiver commands: %s\n"%str(tcp_receiver_cmds))
    tcp_cmds = []

    for cmd in tcp_receiver_cmds:
        tcp_cmds.append(tcp_receiver_cmds[cmd])
    for cmd in tcp_sender_cmds:
        tcp_cmds.append(tcp_sender_cmds[cmd])

    print("running tcp processes")
    outputs = run_tcp_processes(tcp_cmds)

    print(outputs)
    receiver_outputs = [res for res in outputs\
               if (int(res.split()[0]) >= 0 and "receiver" in res)]


    stats_cmds = get_stats_cmds()
    stats_outputs = run_stats_processes(receiver_outputs, stats_cmds)

    print stats_outputs

    # for output in receiver_outputs:
    #     ret, fName, errName = output.split()
    #     os.remove(fName)
    #     os.remove(errName)
    # # print("getting stats commands: %s\n"%str(stats_cmds))
    # # print("running stats processes")
    # # run_stats_processes(tcp_cmds, output, stats_cmds)    
