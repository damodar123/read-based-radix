/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#ifndef HPCJOIN_TASKS_HISTOGRAMCOMPUTATION_H_
#define HPCJOIN_TASKS_HISTOGRAMCOMPUTATION_H_

#include <hpcjoin/tasks/Task.h>
#include <hpcjoin/data/Relation.h>
#include <hpcjoin/histograms/GlobalHistogram.h>
#include <hpcjoin/histograms/LocalHistogram.h>
#include <hpcjoin/histograms/AssignmentMap.h>
#include <hpcjoin/histograms/OffsetMap.h>

using namespace hpcjoin::histograms;

namespace hpcjoin {
namespace tasks {

class HistogramComputation : public Task {

public:

	HistogramComputation(uint32_t numberOfNodes, uint32_t nodeId, hpcjoin::data::Relation *innerRelation, hpcjoin::data::Relation *outerRelation);
	~HistogramComputation();

public:

	void execute();
	task_type_t getType();

protected:

	void computeLocalHistograms();
	void computeGlobalInformation();

public:

	uint32_t *getAssignment();
	uint64_t *getInnerRelationLocalHistogram();
	uint64_t *getOuterRelationLocalHistogram();
	offsetandsizes_t *getOffsetAndSize();

protected:

	uint32_t nodeId;
	uint32_t numberOfNodes;

	hpcjoin::data::Relation *innerRelation;
	hpcjoin::data::Relation *outerRelation;

	hpcjoin::histograms::LocalHistogram *innerRelationLocalHistogram;
	hpcjoin::histograms::LocalHistogram *outerRelationLocalHistogram;

	hpcjoin::histograms::GlobalHistogram *innerRelationGlobalHistogram;
	hpcjoin::histograms::GlobalHistogram *outerRelationGlobalHistogram;

	hpcjoin::histograms::AssignmentMap *assignment;

	hpcjoin::histograms::OffsetMap *offsetandsize;

};

} /* namespace tasks */
} /* namespace hpcjoin */

#endif /* HPCJOIN_TASKS_HISTOGRAMCOMPUTATION_H_ */
