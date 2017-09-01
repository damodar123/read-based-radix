/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#ifndef HPCJOIN_HISTOGRAMS_OFFSETMAP_H_
#define HPCJOIN_HISTOGRAMS_OFFSETMAP_H_

#include <stdint.h>

#include <hpcjoin/histograms/LocalHistogram.h>
#include <hpcjoin/histograms/GlobalHistogram.h>
#include <hpcjoin/histograms/AssignmentMap.h>

namespace hpcjoin {
namespace histograms {

	typedef struct {
		uint64_t partitonOffsetInner;
		uint64_t partitionSizeInner;
		uint64_t partitonOffsetOuter;
		uint64_t partitionSizeOuter;

	}offsetandsizes_t;

class OffsetMap {

public:

	OffsetMap(uint32_t numberOfProcesses, hpcjoin::histograms::LocalHistogram *innerHistogram, hpcjoin::histograms::LocalHistogram *outerHistogram, hpcjoin::histograms::AssignmentMap *assignment);
	~OffsetMap();

public:

	void computeOffsets();

public:

	offsetandsizes_t *getOffsetAndSize();
	void computeOffsetAndSize();

protected:

	uint32_t numberOfProcesses;
	hpcjoin::histograms::LocalHistogram *innerHistogram;
	hpcjoin::histograms::LocalHistogram *outerHistogram;
	hpcjoin::histograms::AssignmentMap *assignment;

	offsetandsizes_t *offsetAndSize;

};

} /* namespace histograms */
} /* namespace hpcjoin */

#endif /* HPCJOIN_HISTOGRAMS_OFFSETMAP_H_ */
