/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#ifndef HPCJOIN_TASKS_BUILDPROBE_H_
#define HPCJOIN_TASKS_BUILDPROBE_H_

#include <stdint.h>

#include <hpcjoin/tasks/Task.h>
#include <hpcjoin/data/CompressedTuple.h>

namespace hpcjoin {
namespace tasks {

class BuildProbe : public Task {

public:

	BuildProbe(uint64_t innerPartitionSize);
	~BuildProbe();

public:

	void execute();
	task_type_t getType();
	void buildHT(uint64_t innerPartSize, hpcjoin::data::CompressedTuple *innerPart);
	//void probeHT();

protected:

	uint64_t innerPartitionSize;
	uint64_t *hashTableNext;
	uint64_t *hashTableBucket;

	uint32_t keyShift;
	uint32_t shiftBits;
	uint64_t MASK;

};

} /* namespace tasks */
} /* namespace hpcjoin */

#endif /* HPCJOIN_TASKS_BUILDPROBE_H_ */
