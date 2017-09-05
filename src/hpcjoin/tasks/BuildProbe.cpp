/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#include "BuildProbe.h"

#include <stdlib.h>

#include <hpcjoin/operators/HashJoin.h>
#include <hpcjoin/core/Configuration.h>
#include <hpcjoin/utils/Debug.h>
#include <hpcjoin/performance/Measurements.h>

#define NEXT_POW_2(V)                           \
    do {                                        \
        V--;                                    \
        V |= V >> 1;                            \
        V |= V >> 2;                            \
        V |= V >> 4;                            \
        V |= V >> 8;                            \
        V |= V >> 16;                           \
        V++;                                    \
    } while(0)

#define HASH_BIT_MODULO(KEY, MASK, NBITS) (((KEY) & (MASK)) >> (NBITS))

namespace hpcjoin {
namespace tasks {

BuildProbe::BuildProbe(uint64_t innerPartitionSize) {

	this->innerPartitionSize = innerPartitionSize;
	this->keyShift = hpcjoin::core::Configuration::NETWORK_PARTITIONING_FANOUT + hpcjoin::core::Configuration::PAYLOAD_BITS;
	this->shiftBits =  this->keyShift + hpcjoin::core::Configuration::LOCAL_PARTITIONING_FANOUT;
	uint64_t N = innerPartitionSize;
	NEXT_POW_2(N);
	this->MASK = (N-1) << (this->shiftBits);

	this->hashTableNext = (uint64_t*) calloc(innerPartitionSize, sizeof(uint64_t));
	this->hashTableBucket = (uint64_t*) calloc(N, sizeof(uint64_t));

}

BuildProbe::~BuildProbe() {

	free(this->hashTableNext);
	free(this->hashTableBucket);
}

void BuildProbe::execute() {
}

void BuildProbe::buildHT(uint64_t innerPartSize, hpcjoin::data::CompressedTuple *innerPart) {

#ifdef MEASUREMENT_DETAILS_LOCALBP
	hpcjoin::performance::Measurements::startBuildProbeTask();
#endif

	// Build hash table
	JOIN_DEBUG("Build-Probe", "Building Hash table of size %lu", innerPartSize);

#ifdef MEASUREMENT_DETAILS_LOCALBP
	hpcjoin::performance::Measurements::startBuildProbeBuild();
#endif

	for (uint64_t t=0; t<innerPartSize;) {
		uint64_t idx = HASH_BIT_MODULO(innerPart[t].value, this->MASK, this->shiftBits);
		this->hashTableNext[t] = this->hashTableBucket[idx];
		this->hashTableBucket[idx]  = ++t;
	}

#ifdef MEASUREMENT_DETAILS_LOCALBP
	hpcjoin::performance::Measurements::stopBuildProbeBuild(innerPartSize);
#endif
}

#if 0
void BuildProbe::probeHT() {

	JOIN_DEBUG("Build-Probe", "Building Hash table of size %lu", outerPartitionSize);
	// Probe hash table

#ifdef MEASUREMENT_DETAILS_LOCALBP
	hpcjoin::performance::Measurements::startBuildProbeProbe();
#endif

	uint64_t matches = 0;
	for (uint64_t t=0; t<this->outerPartitionSize; ++t) {
		uint64_t idx = HASH_BIT_MODULO(outerPartition[t].value, MASK, shiftBits);
		for(uint64_t hit = hashTableBucket[idx]; hit > 0; hit = hashTableNext[hit-1]){
			if((outerPartition[t].value >> keyShift) == (innerPartition[hit-1].value >> keyShift)){
				++matches;
			}
		}
	}

#ifdef MEASUREMENT_DETAILS_LOCALBP
	hpcjoin::performance::Measurements::stopBuildProbeProbe(this->outerPartitionSize);
#endif

	hpcjoin::operators::HashJoin::RESULT_COUNTER += matches;

#ifdef MEASUREMENT_DETAILS_LOCALBP
	hpcjoin::performance::Measurements::stopBuildProbeTask();
#endif

}
#endif

task_type_t BuildProbe::getType() {
	return TASK_BUILD_PROBE;
}

} /* namespace tasks */
} /* namespace hpcjoin */


