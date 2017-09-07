/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#ifndef HPCJOIN_TASKS_NETWORKPARTITIONING_H_
#define HPCJOIN_TASKS_NETWORKPARTITIONING_H_

#include <queue>
#include <hpcjoin/tasks/Task.h>
#include <hpcjoin/tasks/BuildProbe.h>
#include <hpcjoin/data/Window.h>
#include <hpcjoin/data/Relation.h>
#include <hpcjoin/histograms/OffsetMap.h>

using namespace hpcjoin::histograms;

namespace hpcjoin {
namespace tasks {

class NetworkPartitioning : public Task {

public:
	NetworkPartitioning(uint32_t numberOfNodes, uint32_t nodeId, hpcjoin::data::Relation* innerRelation, hpcjoin::data::Relation* outerRelation, uint64_t* innerHistogram, 
		uint64_t* outerHistogram, offsetandsizes_t* offsetAndSize, hpcjoin::data::Window* innerWindow, hpcjoin::data::Window* outerWindow,
		hpcjoin::data::Window* offsetWindow, uint32_t *assignment); 
	~NetworkPartitioning();

public:

	void execute();
	task_type_t getType();
	static std::queue<hpcjoin::tasks::BuildProbe *> TASK_QUEUE;

protected:

	void partition(hpcjoin::data::Relation *relation, hpcjoin::data::Window *window);
	void communicateOffsetandSize(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *innerWindow, hpcjoin::data::Relation *relation, uint64_t* innerHistogram,
			offsetandsizes_t* offsetAndSize);

	void readAndBuild(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *innerWindow);
	void arrangeProbeRelation(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *outerWindow, hpcjoin::data::Relation *relation);
	void readAndProbe(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *outerWindow);

protected:

	uint32_t nodeId;
	uint32_t numberOfNodes;

	hpcjoin::data::Relation *innerRelation;
	hpcjoin::data::Relation *outerRelation;

	uint64_t *innerHistogram;
	uint64_t *outerHistogram;
	offsetandsizes_t *offsetAndSize;

	hpcjoin::data::Window *innerWindow;
	hpcjoin::data::Window *outerWindow;
	hpcjoin::data::Window *offsetWindow;
	uint32_t *assignment;

protected:

	inline static void streamWrite(void *to, void *from)  __attribute__((always_inline));

};

} /* namespace tasks */
} /* namespace hpcjoin */

#endif /* HPCJOIN_TASKS_NETWORKPARTITIONING_H_ */
