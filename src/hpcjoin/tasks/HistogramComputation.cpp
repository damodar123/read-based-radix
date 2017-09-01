/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#include "HistogramComputation.h"

#include <stdlib.h>

#include <hpcjoin/core/Configuration.h>
#include <hpcjoin/utils/Debug.h>

namespace hpcjoin {
namespace tasks {

HistogramComputation::HistogramComputation(uint32_t numberOfNodes, uint32_t nodeId, hpcjoin::data::Relation *innerRelation, hpcjoin::data::Relation *outerRelation) {

	this->nodeId = nodeId;
	this->numberOfNodes = numberOfNodes;

	this->innerRelation = innerRelation;
	this->outerRelation = outerRelation;

	this->innerRelationLocalHistogram = new hpcjoin::histograms::LocalHistogram(innerRelation);
	this->outerRelationLocalHistogram = new hpcjoin::histograms::LocalHistogram(outerRelation);

	this->assignment = new hpcjoin::histograms::AssignmentMap(this->numberOfNodes, NULL, NULL);

	this->offsetandsize = new hpcjoin::histograms::OffsetMap(this->numberOfNodes, this->innerRelationLocalHistogram, this->outerRelationLocalHistogram, this->assignment);

}

HistogramComputation::~HistogramComputation() {

	delete this->innerRelationLocalHistogram;
	delete this->outerRelationLocalHistogram;

	delete this->assignment;

	delete this->offsetandsize;

}

void HistogramComputation::execute() {

	this->innerRelationLocalHistogram->computeLocalHistogram();
	this->outerRelationLocalHistogram->computeLocalHistogram();
	this->assignment->computePartitionAssignment();
	this->offsetandsize->computeOffsetAndSize();

}


uint32_t* HistogramComputation::getAssignment() {

	return this->assignment->getPartitionAssignment();

}

uint64_t* HistogramComputation::getInnerRelationLocalHistogram() {

	return this->innerRelationLocalHistogram->getLocalHistogram();

}

uint64_t* HistogramComputation::getOuterRelationLocalHistogram() {

	return this->outerRelationLocalHistogram->getLocalHistogram();

}

offsetandsizes_t* HistogramComputation::getOffsetAndSize() {

	return this->offsetandsize->getOffsetAndSize();

}

task_type_t HistogramComputation::getType() {
	return TASK_HISTOGRAM;
}

} /* namespace tasks */
} /* namespace hpcjoin */


