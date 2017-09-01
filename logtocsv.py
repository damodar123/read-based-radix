import random
import sys
import os
import time
import subprocess
import signal
import csv
from glob import glob

def main():
    # collect i/p and o/p file names 
    input_file = sys.argv[1]
    output_file = sys.argv[2]

    file_in = open(input_file,'r')

    rows = [];
    with open(output_file, 'wb') as csvfile:
        file_out = csv.writer(csvfile, delimiter=',')
	header = ["Date & Time, NumOfNodes, ProcessesPerNode, TotalProcesses, Hosts, TuplesPerProcess, TotalJoin(ms), maxHistogram(ms), maxNetworkPartition(ms), maxLocalProcessing(ms), maxWindowAlloc(ms), WaitForIncomingData(ms), imaxPrepareForLocalProcessing(ms), maxLocalPartitioning(ms), maxBuild&Probe(ms)".split(','), " "]
        file_out.writerow(header[0]) 

        for line in file_in:
    	    if line.startswith("[RESULTS] Summary"):
	        file_out.writerow(rows)
		rows = [];
		continue

    	    if line.startswith("#DATE"):
	        value = line.split('-')
	        rows.insert(0,value[1].strip('\n'))
	    elif line.startswith("##### mpiexec"):
	        value = line.split(' ')
	        rows.extend([int(float(value[3]))/int(float(value[5])), int(float(value[5])), int(float(value[3]))])
	    elif line.startswith("##[Proc Names]"):
	        value = line.split(':')
	        rows.extend([value[1].strip('\n')])
	    elif line.startswith("[RESULTS]"):
	        value = line.split(':')
		val = value[1].lstrip()
		val = val.strip('\n')
		val = val[:-1]
	        maxval = max(val.split('\t'))
	        rows.extend([maxval])

if __name__ == "__main__":
    main()
