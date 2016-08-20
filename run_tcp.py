import subprocess
import argparse
import os
import sys
import time

parser = argparse.ArgumentParser(description='Run Tcp sender/ receiver')

parser.add_argument('--duration', type=int, help='seconds', default=5)
parser.add_argument('--interArrivalTime', help='seconds', type=float, default=1)
parser.add_argument('--size', type=float, help='(number of packets)', default=1)
parser.add_argument('--numFlows', type=float, default=10000)
parser.add_argument('--sender', action='store_true')
parser.add_argument('--receiver', action='store_true')

parser.add_argument('--rtt', type=float, default=0)
parser.add_argument('--tcpBenchmarkDir', type=str, default="/home/lavanyaj/tcp-benchmark")
parser.add_argument('--percswitchDir', type=str, default="/home/lavanyaj/perc_switch")

# not used yet
parser.add_argument('--dstIp', type=str, default="10.0.0.2")
parser.add_argument('--srcIp', type=str, default="10.0.0.1")
parser.add_argument('--portNum', type=float, help='portNum', default=1)
parser.add_argument('--custom', action='store_true')
parser.add_argument('--scaling', type=float, default=1.0)

args = parser.parse_args()


id_ = 0
cmd = ""
tmpFile = "%s/out.txt"%args.tcpBenchmarkDir
fctFile = "%s/demo/fct_file.csv"%args.percswitchDir

if (args.sender):
    tcpSender = "%s/tcp_sender"%args.tcpBenchmarkDir
    cmd = tcpSender + " " + str(args.duration) + " " + str(args.interArrivalTime) +\
                            " " + str(id_) + " " + args.dstIp + " "\
                            " " + str(args.size) + " " + str(args.numFlows)
elif (args.receiver):
    tcpReceiver = "%s/tcp_receiver"%args.tcpBenchmarkDir
    cmd = tcpReceiver + " " + str(args.duration) + " > " + tmpFile
else:
    cmd = ""
    
print cmd

if len(cmd) > 0:
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
    (out, err) = proc.communicate()

if (args.receiver):
    rtt = args.rtt
    cmd = {}
    cmd["medium_tail"] = "grep fct %s | sed 's/ULL//g' | awk '{ if ($10 * 1500 >= 1e4 && $10 * 1500 < 1e6) {print $8 \" \" $10*1.2;}}' | awk '{print $1/(%f+$2);}' | Rscript -e 'quantile (as.numeric (readLines (\"stdin\")), probs=c(0.5, 0.95))' | tail -n 1 | awk '{print $2;}'" % (tmpFile,rtt)

    cmd["medium_median"]= "grep fct %s | sed 's/ULL//g' | awk '{ if ($10 * 1500 >= 1e4 && $10 * 1500 < 1e6) {print $8 \" \" $10*1.2;}}' | awk '{print $1/(%f+$2);}' | Rscript -e 'quantile (as.numeric (readLines (\"stdin\")), probs=c(0.5, 0.95))' | tail -n 1 | awk '{print $1;}'" % (tmpFile,rtt)

    cmd["small_tail"] = "grep fct %s | sed 's/ULL//g' | awk '{ if ($10 * 1500 < 1e4) {print $8 \" \" $10*1.2;}}' | awk '{print $1/(%f+$2);}' | Rscript -e 'quantile (as.numeric (readLines (\"stdin\")), probs=c(0.5, 0.95))' | tail -n 1 | awk '{print $2;}'" % (tmpFile,rtt)

    cmd["small_median"] = "grep fct %s | sed 's/ULL//g' | awk '{ if ($10 * 1500 < 1e4) {print $8 \" \" $10*1.2;}}' | awk '{print $1/(%f+$2);}' | Rscript -e 'quantile (as.numeric (readLines (\"stdin\")), probs=c(0.5, 0.95))' | tail -n 1 | awk '{print $1;}'" % (tmpFile,rtt)

    cmd["large_tail"] = "grep fct %s | sed 's/ULL//g' | awk '{ if ($10 * 1500 >= 1e6) {print $8 \" \" $10*1.2;}}' | awk '{print $1/(%f+$2);}' | Rscript -e 'quantile (as.numeric (readLines (\"stdin\")), probs=c(0.5, 0.95))' | tail -n 1 | awk '{print $2;}'" % (tmpFile,rtt)

    cmd["large_median"] = "grep fct %s | sed 's/ULL//g' | awk '{ if ($10 * 1500 >= 1e6) {print $8 \" \" $10*1.2;}}' | awk '{print $1/(%f+$2);}' | Rscript -e 'quantile (as.numeric (readLines (\"stdin\")), probs=c(0.5, 0.95))' | tail -n 1 | awk '{print $1;}'" % (tmpFile,rtt)

    val = {}
    for k in cmd:
        v = cmd[k]
        pipedCommands = v.split("|")
        #print pipedCommands
        proc = subprocess.Popen(v, stdout=subprocess.PIPE, shell=True)
        (out, err) = proc.communicate()
        if (err is not None):
            print ("error running " + str(cmd[k]))
        val[k] = out
        #print k, ": ", out
    
    current_rate = 10000
    current_alg = "tcp"
    fct_small = val["small_tail"]
    fct_medium = val["medium_tail"]
    fct_large = val["large_median"]
    update_id = int(round(time.time()))
    
    f = open(fctFile, "w")
    result = (",".join([str(x).rstrip() for x in [current_rate,current_alg,fct_small,fct_medium,fct_large,update_id]]))
    print(result)
    f.write(result)
    f.close()
