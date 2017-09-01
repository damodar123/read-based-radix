import random
import sys
import os
import time
import subprocess
import signal
from glob import glob

def cross_prod_two(set1, set2):
	ret = [];
	for el1 in set1:
		for el2 in set2:
			ret.append(el1+[el2]);

	return ret;

def cross_prod(sets):
	return reduce(cross_prod_two, sets, [[]]);


if (len(sys.argv) < 2):
	print "USAGE: run.py outputfile"
	exit()

outputfile = sys.argv[1]
print "Redirecting stdout to \"" + outputfile + "\"."
output = open(outputfile, "a");



expcubes = [
	(
		("mpiexec", ),
		("-np", ),
		("1", "2", "4", "8", ),
		("-ppn", ),
		("1", "2", "4", "8", ),
		("./release/cahj-bin", ),
	)];

iterations = 10
secondsbetweenpolls = 15
minutesfortimeout = 10

experiments = [];
for cube in expcubes:
	experiments+=(cross_prod(cube) * iterations);
random.shuffle(experiments);

totalexp = len(experiments);
counter = 0;

for run in experiments:
	mn = int(float(run[2]))*int(float(run[4]))
	if mn == 1:
		continue
	cmd = []
	cmd.extend([run[0], run[1], mn, run[3], run[4], run[5]])
	executable = " ".join([str(i) for i in cmd])
	"".join(str(l) for l in executable)
	counter += 1

	print time.ctime();
	print str(counter) + "/" + str(totalexp), "(" + str(int(float(counter*100)/totalexp)) + "%): Executing \"" + executable + "\"."
        sys.stdout.flush();
	print >> output, "#DATE -", time.ctime();
	print >> output, "#####", executable
	output.flush();

	handle = subprocess.Popen("( " + executable + " )", close_fds=True, preexec_fn=os.setpgrp,
				shell=True, stdout=output, stderr=subprocess.STDOUT);

	termin = True
	time.sleep(1)
	for pollround in range(minutesfortimeout*60/secondsbetweenpolls+1):
		if (handle.poll() is not None):
			termin = False
			break;
		time.sleep(secondsbetweenpolls)
	
	if (termin):
		print "Terminating process due to timeout..."
                sys.stdout.flush();
		os.killpg(handle.pid, signal.SIGTERM)
		handle.wait()
		print >> output, "> Process terminated due to timeout."

        output.flush();
        print >> output, "---- PS INFO START ----"
        f = os.popen('ps -ef | grep -v "^[ ]*root"');
        for line in f:
               print >> output, line,
        f.close();
        print >> output, "---- PS INFO END ----"

	output.flush();
	os.fsync(output.fileno())

	time.sleep(5);	# cooldown time
output.close();

