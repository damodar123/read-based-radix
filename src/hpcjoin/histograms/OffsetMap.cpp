/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#include "OffsetMap.h"

#include <stdlib.h>
#include <mpi.h>

#include <hpcjoin/core/Configuration.h>
#include <hpcjoin/performance/Measurements.h>

namespace hpcjoin {
namespace histograms {


OffsetMap::OffsetMap(uint32_t numberOfProcesses, LocalHistogram* innerHistogram, LocalHistogram* outerHistogram, AssignmentMap* assignment) {

	this->numberOfProcesses = numberOfProcesses;
	this->innerHistogram = innerHistogram;
	this->outerHistogram = outerHistogram;
	this->assignment = assignment;

	this->offsetAndSize = (offsetandsizes_t *) calloc(hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT, sizeof(offsetandsizes_t));

}

OffsetMap::~OffsetMap() {

	free(this->offsetAndSize);
}

void OffsetMap::computeOffsetAndSize() {

#ifdef MEASUREMENT_DETAILS_HISTOGRAM
	hpcjoin::performance::Measurements::startHistogramOffsetComputation();
#endif

	uint64_t *innerhistogram = this->innerHistogram->getLocalHistogram();
	uint64_t *outerhistogram = this->outerHistogram->getLocalHistogram();

	this->offsetAndSize[0].partitonOffsetInner = 0;
	this->offsetAndSize[0].partitionSizeInner = innerhistogram[0];
	this->offsetAndSize[0].partitonOffsetOuter = 0;
	this->offsetAndSize[0].partitionSizeOuter = outerhistogram[0];
	for (uint32_t i = 1; i < hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT; ++i) {
		this->offsetAndSize[i].partitonOffsetInner = this->offsetAndSize[i-1].partitonOffsetInner + innerhistogram[i];
		this->offsetAndSize[i].partitionSizeInner = innerhistogram[i];
		this->offsetAndSize[i].partitonOffsetOuter = this->offsetAndSize[i-1].partitonOffsetOuter + outerhistogram[i];
		this->offsetAndSize[i].partitionSizeOuter = outerhistogram[i];
	}

#ifdef MEASUREMENT_DETAILS_HISTOGRAM
	hpcjoin::performance::Measurements::stopHistogramOffsetComputation();
#endif

}



offsetandsizes_t* OffsetMap::getOffsetAndSize() {

	return offsetAndSize;

}

} /* namespace histograms */
} /* namespace hpcjoin */
