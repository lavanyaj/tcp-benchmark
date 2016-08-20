import subprocess
import argparse

parser = argparse.ArgumentParser(description='Run Tcp sender/ receiver')

parser.add_argument('--duration', type=int, help='seconds', default=5)
parser.add_argument('--interArrivalTime', help='seconds', type=float, default=1)
parser.add_argument('--size', type=float, help='(number of packets)', default=1)
parser.add_argument('--numFlows', type=float, default=10000)
parser.add_argument('--tcpBenchmarkDir', type=str, default="/home/lavanyaj/tcp-benchmark")
parser.add_argument('--sender', action='store_true')
parser.add_argument('--receiver', action='store_true')


parser.add_argument('--dstIp', type=str, default="10.0.0.2")
parser.add_argument('--srcIp', type=str, default="10.0.0.1")
parser.add_argument('--portNum', type=float, help='portNum', default=1)
parser.add_argument('--custom', action='store_true')
parser.add_argument('--scaling', type=float, default=1.0)
parser.add_argument('--rtt', type=float, default=0.0001)

parser.add_argument('--percswitchDir', type=str, default="/home/lavanyaj/perc_switch")

args = parser.parse_args()


id_ = 0
selectType = 0

cmd = ""

if (args.sender):
    tcpSender = "%s/tcp_sender"%args.tcpBenchmarkDir
    cmd = tcpSender + " " + str(args.duration) + " " + str(args.interArrivalTime) +\
                            " " + str(id_) + " " + args.dstIp + " " + args.srcIp +\
                            " " + str(args.size) + " " + str(args.numFlows)
elif (args.receiver):
    tcpReceiver = "%s/tcp_receiver"%args.tcpBenchmarkDir
    cmd = tcpReceiver + " " + str(args.duration) +
else:
    cmd = ""
    
print cmd

if len(cmd) > 0:
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
    (out, err) = proc.communicate()
