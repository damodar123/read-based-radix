/**
 * @author  Claude Barthels <claudeb@inf.ethz.ch>
 * (c) 2016, ETH Zurich, Systems Group
 *
 */

#include "NetworkPartitioning.h"

#include <immintrin.h>
#include <stdlib.h>
#include <string.h>

#include <hpcjoin/core/Configuration.h>
#include <hpcjoin/data/CompressedTuple.h>
#include <hpcjoin/utils/Debug.h>
#include <hpcjoin/performance/Measurements.h>

#define NETWORK_PARTITIONING_CACHELINE_SIZE (64)
#define TUPLES_PER_CACHELINE (NETWORK_PARTITIONING_CACHELINE_SIZE / sizeof(hpcjoin::data::CompressedTuple))

#define HASH_BIT_MODULO(KEY, MASK, NBITS) (((KEY) & (MASK)) >> (NBITS))

#define PARTITION_ACCESS(p) (((char *) inMemoryBuffer) + (p * hpcjoin::core::Configuration::MEMORY_PARTITION_SIZE_BYTES))

namespace hpcjoin {
namespace tasks {

std::queue<hpcjoin::tasks::BuildProbe *> NetworkPartitioning::TASK_QUEUE;

typedef union {

	struct {
		hpcjoin::data::CompressedTuple tuples[TUPLES_PER_CACHELINE];
	} tuples;

	struct {
		hpcjoin::data::CompressedTuple tuples[TUPLES_PER_CACHELINE - 1];
		uint32_t inCacheCounter;
		uint32_t memoryCounter;
	} data;

} cacheline_t;

NetworkPartitioning::NetworkPartitioning(uint32_t numberOfNodes, uint32_t nodeId, hpcjoin::data::Relation* innerRelation, hpcjoin::data::Relation* outerRelation, uint64_t* innerHistogram, 
		uint64_t* outerHistogram, offsetandsizes_t* offsetAndSize, hpcjoin::data::Window* innerWindow, hpcjoin::data::Window* outerWindow, 
		hpcjoin::data::Window* offsetWindow, uint32_t* assignment) {

	this->nodeId = nodeId;
	this->numberOfNodes = numberOfNodes;

	this->innerRelation = innerRelation;
	this->outerRelation = outerRelation;

	this->innerHistogram = innerHistogram;
	this->outerHistogram = outerHistogram;
	this->offsetAndSize = offsetAndSize;

	this->innerWindow = innerWindow;
	this->outerWindow = outerWindow;
	this->offsetWindow = offsetWindow;

	this->assignment = assignment;

	JOIN_ASSERT(hpcjoin::core::Configuration::CACHELINE_SIZE_BYTES == NETWORK_PARTITIONING_CACHELINE_SIZE, "Network Partitioning", "Cache line sizes do not match. This is a hack and the value needs to be edited in two places.");

}

NetworkPartitioning::~NetworkPartitioning() {
}

void NetworkPartitioning::execute() {

	JOIN_DEBUG("Network Partitioning", "Node %d is communicating Offsets and Size of both the relations", this->nodeId);
	communicateOffsetandSize(offsetWindow, innerWindow, innerRelation, innerHistogram, offsetAndSize);
	MPI_Barrier(MPI_COMM_WORLD); 

#if 0
	offsetandsizes_t * read_data = (offsetandsizes_t *)offsetWindow->data;

	for (uint32_t p = 0; p < hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT; ++p)
	{
		printf(" Node =%d, partitionID=%d, Assigned=%d, Inner offset=%d, size=%d Outer offset=%d, size=%d\n", this->nodeId, p, this->assignment[p],
				read_data[p].partitionOffsetInner, read_data[p].partitionSizeInner, read_data[p].partitionOffsetOuter, read_data[p].partitionSizeOuter);
	}
	printf("Communication DONE \n");
	return;
#endif

	readAndBuild(offsetWindow, innerWindow);
	arrangeProbeRelation(offsetWindow, outerWindow, outerRelation);
	MPI_Barrier(MPI_COMM_WORLD); 
	readAndProbe(offsetWindow, outerWindow);
	printf("Communication DONE \n");
	return;
	JOIN_DEBUG("Network Partitioning", "Node %d is partitioning inner relation", this->nodeId);
	partition(innerRelation, innerWindow);

	JOIN_DEBUG("Network Partitioning", "Node %d is partitioning outer relation", this->nodeId);
	partition(outerRelation, outerWindow);

}

//Probe into the hash table
void NetworkPartitioning::readAndProbe(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *outerWindow)
{
	outerWindow->start();

	uint64_t *sum;
	uint64_t max = 0;
	uint32_t assignedCount = hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT/this->numberOfNodes;
	//Imbalance of assigned partitions.
	if(this->nodeId < (hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT - this->numberOfNodes*assignedCount))
	{
		assignedCount++;
	}
	hpcjoin::tasks::BuildProbe *buildProbeArray[assignedCount];
	sum = (uint64_t *)calloc(assignedCount, sizeof(uint64_t));
	for (uint32_t a = 0; a < assignedCount; ++a)
	{
		offsetandsizes_t *assignedPartition = (offsetandsizes_t *)offsetWindow->data + this->numberOfNodes*a;
		for (uint32_t n = 0; n < this->numberOfNodes; ++n)
		{
			sum[a] += assignedPartition[n].partitionSizeOuter;
			if (max < assignedPartition[n].partitionSizeOuter)
			{
				max = assignedPartition[n].partitionSizeOuter;
			}
		}
	}
	//Increase the request size and use MPI_Waitany to wait on the array.
	MPI_Request req[2] = { MPI_REQUEST_NULL };
	//create the buffer of maximum read size
	hpcjoin::data::CompressedTuple * readBuffer[2];
	readBuffer[0] = (hpcjoin::data::CompressedTuple *)calloc(max, sizeof(hpcjoin::data::CompressedTuple));
	readBuffer[1] = (hpcjoin::data::CompressedTuple *)calloc(max, sizeof(hpcjoin::data::CompressedTuple));

	for(uint32_t a = 0; a < assignedCount; ++a)
	{
		uint32_t i = 0;

		hpcjoin::tasks::BuildProbe* task = TASK_QUEUE.front();
		TASK_QUEUE.pop();
		
		offsetandsizes_t *offsetAndSize = (offsetandsizes_t *)offsetWindow->data + this->numberOfNodes*a;
		MPI_Rget(readBuffer[i%2], offsetAndSize[i].partitionSizeOuter*sizeof(hpcjoin::data::CompressedTuple), MPI_CHAR, i, 
				offsetAndSize[i].partitionOffsetOuter, offsetAndSize[i].partitionSizeOuter*sizeof(hpcjoin::data::CompressedTuple),
				MPI_CHAR, *outerWindow->window, &req[i%2]);
		for (i = 1; i < this->numberOfNodes; ++i) 
		{
			MPI_Rget(readBuffer[i%2], offsetAndSize[i].partitionSizeOuter*sizeof(hpcjoin::data::CompressedTuple), MPI_CHAR, i, 
					offsetAndSize[i].partitionOffsetOuter, offsetAndSize[i].partitionSizeOuter*sizeof(hpcjoin::data::CompressedTuple),
					MPI_CHAR, *outerWindow->window, &req[i%2]);
			MPI_Wait(&req[(i-1)%2],MPI_STATUS_IGNORE);
			task->probeHT(offsetAndSize[i-1].partitionSizeOuter, readBuffer[(i-1)%2]);
		}
		MPI_Wait(&req[(i-1)%2],MPI_STATUS_IGNORE);
		task->probeHT(offsetAndSize[i-1].partitionSizeOuter, readBuffer[(i-1)%2]);

		delete task;
	}
	free(sum);
	free(readBuffer[1]);
	free(readBuffer[0]);

	outerWindow->flush();
	outerWindow->stop();
}

//Arrange probe realtion data in the window for the other processes to read from.
void NetworkPartitioning::arrangeProbeRelation(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *outerWindow, hpcjoin::data::Relation *relation)
{
	uint64_t const numberOfElements = relation->getLocalSize();
	hpcjoin::data::Tuple * const data = relation->getData();
	uint64_t const partitionCount = hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT;
	const uint32_t partitionBits = hpcjoin::core::Configuration::NETWORK_PARTITIONING_FANOUT;
	cacheline_t inCacheBuffer[hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT] __attribute__((aligned(NETWORK_PARTITIONING_CACHELINE_SIZE)));;

	JOIN_DEBUG("Network Partitioning", "Node %d is setting counter to zero", this->nodeId);

	for (uint64_t p = 0; p < hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT; ++p)
	{
		inCacheBuffer[p].data.inCacheCounter = 0;
		inCacheBuffer[p].data.memoryCounter = 0;
	}

	for (uint64_t i = 0; i < numberOfElements; ++i) 
	{
		uint32_t partitionId = HASH_BIT_MODULO(data[i].key, hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT - 1, 0);

		uint32_t inCacheCounter = inCacheBuffer[partitionId].data.inCacheCounter;
		uint32_t memoryCounter = inCacheBuffer[partitionId].data.memoryCounter;

		hpcjoin::data::CompressedTuple *cacheLine = (hpcjoin::data::CompressedTuple *) (inCacheBuffer + partitionId);
		//cacheLine[inCacheCounter] = data[i];
		cacheLine[inCacheCounter].value = data[i].rid + ((data[i].key >> partitionBits) << (partitionBits + hpcjoin::core::Configuration::PAYLOAD_BITS));
		++inCacheCounter;

		// Check if cache line is full
		if (inCacheCounter == TUPLES_PER_CACHELINE) 
		{
			// Move cache line to memory buffer
			char *inMemoryStreamDestination = (((char *) outerWindow->data) + (offsetAndSize[partitionId].partitionOffsetOuter)) + (memoryCounter * NETWORK_PARTITIONING_CACHELINE_SIZE);
			memcpy(inMemoryStreamDestination, cacheLine, NETWORK_PARTITIONING_CACHELINE_SIZE);

			++memoryCounter;
			inCacheCounter = 0;
		}

		inCacheBuffer[partitionId].data.inCacheCounter = inCacheCounter;
		inCacheBuffer[partitionId].data.memoryCounter = memoryCounter;

	}
}

#if 1 
//with MPI_Rget ====
void NetworkPartitioning::readAndBuild(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *innerWindow)
{
	innerWindow->start();

	uint64_t *sum;
	uint64_t max = 0;
	uint32_t assignedCount = hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT/this->numberOfNodes;
	//Imbalance of assigned partitions.
	if(this->nodeId < (hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT - this->numberOfNodes*assignedCount))
	{
		assignedCount++;
	}
	hpcjoin::tasks::BuildProbe *buildProbeArray[assignedCount];
	sum = (uint64_t *)calloc(assignedCount, sizeof(uint64_t));
	for (uint32_t a = 0; a < assignedCount; ++a)
	{
		offsetandsizes_t *assignedPartition = (offsetandsizes_t *)offsetWindow->data + this->numberOfNodes*a;
		for (uint32_t n = 0; n < this->numberOfNodes; ++n)
		{
			sum[a] += assignedPartition[n].partitionSizeInner;
			if (max < assignedPartition[n].partitionSizeInner)
			{
				max = assignedPartition[n].partitionSizeInner;
			}
		}
	}
	//Increase the request size and use MPI_Waitany to wait on the array.
	MPI_Request req[2] = { MPI_REQUEST_NULL };
	//create the buffer of maximum read size
	hpcjoin::data::CompressedTuple * readBuffer[2];
	readBuffer[0] = (hpcjoin::data::CompressedTuple *)calloc(max, sizeof(hpcjoin::data::CompressedTuple));
	readBuffer[1] = (hpcjoin::data::CompressedTuple *)calloc(max, sizeof(hpcjoin::data::CompressedTuple));

	for(uint32_t a = 0; a < assignedCount; ++a)
	{
		uint32_t i = 0;

		buildProbeArray[a] = new hpcjoin::tasks::BuildProbe(sum[a]);
		
		offsetandsizes_t *offsetAndSize = (offsetandsizes_t *)offsetWindow->data + this->numberOfNodes*a;
		MPI_Rget(readBuffer[i%2], offsetAndSize[i].partitionSizeInner*sizeof(hpcjoin::data::CompressedTuple), MPI_CHAR, i, 
				offsetAndSize[i].partitionOffsetInner, offsetAndSize[i].partitionSizeInner*sizeof(hpcjoin::data::CompressedTuple),
				MPI_CHAR, *innerWindow->window, &req[i%2]);
		for (i = 1; i < this->numberOfNodes; ++i) 
		{
			MPI_Rget(readBuffer[i%2], offsetAndSize[i].partitionSizeInner*sizeof(hpcjoin::data::CompressedTuple), MPI_CHAR, i, 
					offsetAndSize[i].partitionOffsetInner, offsetAndSize[i].partitionSizeInner*sizeof(hpcjoin::data::CompressedTuple),
					MPI_CHAR, *innerWindow->window, &req[i%2]);
			MPI_Wait(&req[(i-1)%2],MPI_STATUS_IGNORE);
			buildProbeArray[a]->buildHT(offsetAndSize[i-1].partitionSizeInner, readBuffer[(i-1)%2]);
		}
		MPI_Wait(&req[(i-1)%2],MPI_STATUS_IGNORE);
		buildProbeArray[a]->buildHT(offsetAndSize[i-1].partitionSizeInner, readBuffer[(i-1)%2]);

		TASK_QUEUE.push(buildProbeArray[a]);
	}
	free(sum);
	free(readBuffer[1]);
	free(readBuffer[0]);

	innerWindow->flush();
	innerWindow->stop();
}
#endif


#if 0
void NetworkPartitioning::readAndBuild(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *innerWindow)
{
	printf("Node=%d start readAndBuild\n", this->nodeId);
	innerWindow->start();

	uint64_t *sum;
	uint64_t max = 0;
	uint32_t assignedCount = hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT/this->numberOfNodes;
	//Imbalance of assigned partitions.
	if(this->nodeId < (hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT - this->numberOfNodes*assignedCount))
	{
		assignedCount++;
	}
	hpcjoin::tasks::BuildProbe *buildProbeArray[assignedCount];
	sum = (uint64_t *)calloc(assignedCount, sizeof(uint64_t));
	for (uint32_t a = 0; a < assignedCount; ++a)
	{
		//TODO: Type of the Offset window should not be compressed tuple type.
		offsetandsizes_t *assignedPartition = (offsetandsizes_t *)offsetWindow->data + this->numberOfNodes*a;
		for (uint32_t n = 0; n < this->numberOfNodes; ++n)
		{
			sum[a] += assignedPartition[n].partitionSizeInner;
			if (max < assignedPartition[n].partitionSizeInner)
			{
				max = assignedPartition[n].partitionSizeInner;
			}
		}
	}
	printf("Node=%d, AssignedCount=%d \n", this->nodeId, assignedCount);
	//Increase the request size and use MPI_Waitany to wait on the array.
	MPI_Request req =  MPI_REQUEST_NULL;
	//create the buffer of maximum read size
	hpcjoin::data::CompressedTuple * readBuffer =  (hpcjoin::data::CompressedTuple *)calloc(max, sizeof(hpcjoin::data::CompressedTuple));

	for(uint32_t a = 0; a < assignedCount; ++a)
	{
		uint32_t i = 0;
		printf("Node=%d, assignement=%d, max= %d, sum=%d\n", this->nodeId, a, max, sum[a]);

		buildProbeArray[a] = new hpcjoin::tasks::BuildProbe(sum[a]);
		
		printf("Node=%d - assignemnt=%d, point to the offset and size \n", this->nodeId, a);
		offsetandsizes_t *offsetAndSize = (offsetandsizes_t *)offsetWindow->data + this->numberOfNodes*a;
		for (i = 0; i < this->numberOfNodes; ++i) 
		{
			MPI_Rget(readBuffer, offsetAndSize[i].partitionSizeInner*sizeof(hpcjoin::data::CompressedTuple), MPI_CHAR, i, 
					offsetAndSize[i].partitionOffsetInner, offsetAndSize[i].partitionSizeInner*sizeof(hpcjoin::data::CompressedTuple),
					MPI_CHAR, *innerWindow->window, &req);
			MPI_Wait(&req,MPI_STATUS_IGNORE);
			buildProbeArray[a]->buildHT(this->nodeId, offsetAndSize[i-1].partitionSizeInner, readBuffer);
		}
		printf("Node=%d, assignment=%d, before Final wait sizeHT= %d\n", this->nodeId, a, offsetAndSize[i-1].partitionSizeInner);

		TASK_QUEUE.push(buildProbeArray[a]);
		printf("Node=%d, assignment=%d DONE \n", this->nodeId, a);
	}
	free(sum);
	free(readBuffer);

	innerWindow->flush();
	innerWindow->stop();
}

#endif

void NetworkPartitioning::communicateOffsetandSize(hpcjoin::data::Window *offsetWindow, hpcjoin::data::Window *innerWindow, hpcjoin::data::Relation *relation,
		uint64_t* innerHistogram, offsetandsizes_t* offsetAndSize) 
{

	offsetWindow->start();

	uint64_t p;
	uint32_t partitionSlot;
	uint64_t const numberOfElements = relation->getLocalSize();
	hpcjoin::data::Tuple * const data = relation->getData();
	uint64_t const partitionCount = hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT;
	const uint32_t partitionBits = hpcjoin::core::Configuration::NETWORK_PARTITIONING_FANOUT;
	cacheline_t inCacheBuffer[hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT] __attribute__((aligned(NETWORK_PARTITIONING_CACHELINE_SIZE)));;

	JOIN_DEBUG("Network Partitioning", "Node %d is setting counter to zero", this->nodeId);

	for (p = 0; p < hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT; ++p)
	{
		inCacheBuffer[p].data.inCacheCounter = 0;
		inCacheBuffer[p].data.memoryCounter = 0;
	}

	p = 0;
	for (uint64_t i = 0; i < numberOfElements; ++i) 
	{
		uint32_t partitionId = HASH_BIT_MODULO(data[i].key, hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT - 1, 0);

		uint32_t inCacheCounter = inCacheBuffer[partitionId].data.inCacheCounter;
		uint32_t memoryCounter = inCacheBuffer[partitionId].data.memoryCounter;

		hpcjoin::data::CompressedTuple *cacheLine = (hpcjoin::data::CompressedTuple *) (inCacheBuffer + partitionId);
		//cacheLine[inCacheCounter] = data[i];
		cacheLine[inCacheCounter].value = data[i].rid + ((data[i].key >> partitionBits) << (partitionBits + hpcjoin::core::Configuration::PAYLOAD_BITS));
		++inCacheCounter;

		// Check if cache line is full
		if (inCacheCounter == TUPLES_PER_CACHELINE) 
		{
			if (p < hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT)
			{
				partitionSlot = p / this->numberOfNodes;
				MPI_Put(&offsetAndSize[p], sizeof(offsetandsizes_t), MPI_CHAR, this->assignment[p], 
						sizeof(offsetandsizes_t)*(partitionSlot*this->numberOfNodes + this->nodeId),
						sizeof(offsetandsizes_t), MPI_CHAR, *offsetWindow->window);
				p++;
			}
			// Move cache line to memory buffer
			char *inMemoryStreamDestination = (((char *) innerWindow->data) + (offsetAndSize[partitionId].partitionOffsetInner)) + (memoryCounter * NETWORK_PARTITIONING_CACHELINE_SIZE);
			memcpy(inMemoryStreamDestination, cacheLine, NETWORK_PARTITIONING_CACHELINE_SIZE);

			++memoryCounter;
			inCacheCounter = 0;
		}

		inCacheBuffer[partitionId].data.inCacheCounter = inCacheCounter;
		inCacheBuffer[partitionId].data.memoryCounter = memoryCounter;

	}
	if(p < hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT)
	{
		partitionSlot = p / this->numberOfNodes;
		MPI_Put(&offsetAndSize[p], sizeof(offsetandsizes_t), MPI_CHAR, this->assignment[p], 
				sizeof(offsetandsizes_t)*(partitionSlot*this->numberOfNodes + this->nodeId),
				sizeof(offsetandsizes_t), MPI_CHAR, *offsetWindow->window);
		p++;
	}
	offsetWindow->flush();
	offsetWindow->stop();
}


void NetworkPartitioning::partition(hpcjoin::data::Relation *relation, hpcjoin::data::Window *window) {

	window->start();

	uint64_t const numberOfElements = relation->getLocalSize();
	hpcjoin::data::Tuple * const data = relation->getData();

	// Create in-memory buffer
	uint64_t const bufferedPartitionCount = hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT;
	uint64_t const bufferedPartitionSize = hpcjoin::core::Configuration::MEMORY_PARTITION_SIZE_BYTES;
	uint64_t const inMemoryBufferSize = bufferedPartitionCount * bufferedPartitionSize;

	const uint32_t partitionBits = hpcjoin::core::Configuration::NETWORK_PARTITIONING_FANOUT;

#ifdef MEASUREMENT_DETAILS_NETWORK
	hpcjoin::performance::Measurements::startNetworkPartitioningMemoryAllocation();
#endif

	hpcjoin::data::CompressedTuple * inMemoryBuffer = NULL;
	int result = posix_memalign((void **) &(inMemoryBuffer), NETWORK_PARTITIONING_CACHELINE_SIZE, inMemoryBufferSize);

	JOIN_ASSERT(result == 0, "Network Partitioning", "Could not allocate in-memory buffer");
	memset(inMemoryBuffer, 0, inMemoryBufferSize);

#ifdef MEASUREMENT_DETAILS_NETWORK
	hpcjoin::performance::Measurements::stopNetworkPartitioningMemoryAllocation(inMemoryBufferSize);
#endif

	// Create in-cache buffer
	cacheline_t inCacheBuffer[hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT] __attribute__((aligned(NETWORK_PARTITIONING_CACHELINE_SIZE)));;

	JOIN_DEBUG("Network Partitioning", "Node %d is setting counter to zero", this->nodeId);
	for (uint32_t p = 0; p < hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT; ++p) {
		inCacheBuffer[p].data.inCacheCounter = 0;
		inCacheBuffer[p].data.memoryCounter = 0;
	}

#ifdef MEASUREMENT_DETAILS_NETWORK
	hpcjoin::performance::Measurements::startNetworkPartitioningMainPartitioning();
#endif

	for (uint64_t i = 0; i < numberOfElements; ++i) {

		// Compute partition
		uint32_t partitionId = HASH_BIT_MODULO(data[i].key, hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT - 1, 0);

		// Save counter to register
		uint32_t inCacheCounter = inCacheBuffer[partitionId].data.inCacheCounter;
		uint32_t memoryCounter = inCacheBuffer[partitionId].data.memoryCounter;

		// Move data to cache line
		hpcjoin::data::CompressedTuple *cacheLine = (hpcjoin::data::CompressedTuple *) (inCacheBuffer + partitionId);
		//cacheLine[inCacheCounter] = data[i];
		cacheLine[inCacheCounter].value = data[i].rid + ((data[i].key >> partitionBits) << (partitionBits + hpcjoin::core::Configuration::PAYLOAD_BITS));
		++inCacheCounter;

		// Check if cache line is full
		if (inCacheCounter == TUPLES_PER_CACHELINE) {

			//JOIN_DEBUG("Network Partitioning", "Node %d has a full cache line %d", this->nodeId, partitionId);

			// Move cache line to memory buffer
			char *inMemoryStreamDestination = PARTITION_ACCESS(partitionId) + (memoryCounter * NETWORK_PARTITIONING_CACHELINE_SIZE);
			streamWrite(inMemoryStreamDestination, cacheLine);
			++memoryCounter;

			//JOIN_DEBUG("Network Partitioning", "Node %d has completed the stream write of cache line %d", this->nodeId, partitionId);

			// Check if memory buffer is full
			if (memoryCounter % hpcjoin::core::Configuration::CACHELINES_PER_MEMORY_BUFFER == 0) {

				bool rewindBuffer = (memoryCounter == hpcjoin::core::Configuration::MEMORY_BUFFERS_PER_PARTITION * hpcjoin::core::Configuration::CACHELINES_PER_MEMORY_BUFFER);

				//JOIN_DEBUG("Network Partitioning", "Node %d has a full memory buffer %d", this->nodeId, partitionId);
				hpcjoin::data::CompressedTuple *inMemoryBufferLocation = reinterpret_cast<hpcjoin::data::CompressedTuple *>(PARTITION_ACCESS(partitionId) + (memoryCounter * NETWORK_PARTITIONING_CACHELINE_SIZE) - (hpcjoin::core::Configuration::MEMORY_BUFFER_SIZE_BYTES));
				window->write(partitionId, inMemoryBufferLocation, hpcjoin::core::Configuration::CACHELINES_PER_MEMORY_BUFFER * TUPLES_PER_CACHELINE, rewindBuffer);

				if(rewindBuffer) {
					memoryCounter = 0;
				}

				//JOIN_DEBUG("Network Partitioning", "Node %d has completed the put operation of memory buffer %d", this->nodeId, partitionId);
			}

			inCacheCounter = 0;
		}

		inCacheBuffer[partitionId].data.inCacheCounter = inCacheCounter;
		inCacheBuffer[partitionId].data.memoryCounter = memoryCounter;

	}

#ifdef MEASUREMENT_DETAILS_NETWORK
	hpcjoin::performance::Measurements::stopNetworkPartitioningMainPartitioning(numberOfElements);
#endif

	JOIN_DEBUG("Network Partitioning", "Node %d is flushing remaining tuples", this->nodeId);

#ifdef MEASUREMENT_DETAILS_NETWORK
	hpcjoin::performance::Measurements::startNetworkPartitioningFlushPartitioning();
#endif

	// Flush remaining elements to memory buffers
	for(uint32_t p=0; p<hpcjoin::core::Configuration::NETWORK_PARTITIONING_COUNT; ++p) {

		uint32_t inCacheCounter = inCacheBuffer[p].data.inCacheCounter;
		uint32_t memoryCounter = inCacheBuffer[p].data.memoryCounter;

		hpcjoin::data::CompressedTuple *cacheLine = (hpcjoin::data::CompressedTuple *) (inCacheBuffer + p);
		hpcjoin::data::CompressedTuple *inMemoryFreeSpace = reinterpret_cast<hpcjoin::data::CompressedTuple *>(PARTITION_ACCESS(p) + (memoryCounter * NETWORK_PARTITIONING_CACHELINE_SIZE));
		for(uint32_t t=0; t<inCacheCounter; ++t) {
				inMemoryFreeSpace[t] = cacheLine[t];
		}

		uint32_t remainingTupleInMemory = ((memoryCounter % hpcjoin::core::Configuration::CACHELINES_PER_MEMORY_BUFFER) * TUPLES_PER_CACHELINE) + inCacheCounter;

		if(remainingTupleInMemory > 0) {
			hpcjoin::data::CompressedTuple *inMemoryBufferOfPartition = reinterpret_cast<hpcjoin::data::CompressedTuple *>(PARTITION_ACCESS(p) + (memoryCounter/hpcjoin::core::Configuration::CACHELINES_PER_MEMORY_BUFFER) * hpcjoin::core::Configuration::MEMORY_BUFFER_SIZE_BYTES);
			window->write(p, inMemoryBufferOfPartition, remainingTupleInMemory, false);
		}

	}

	window->flush();
	window->stop();

	free(inMemoryBuffer);

#ifdef MEASUREMENT_DETAILS_NETWORK
	hpcjoin::performance::Measurements::stopNetworkPartitioningFlushPartitioning();
#endif

	window->assertAllTuplesWritten();

}

inline void NetworkPartitioning::streamWrite(void* to, void* from) {

	JOIN_ASSERT(to != NULL, "Network Partitioning", "Stream destination should not be NULL");
	JOIN_ASSERT(from != NULL, "Network Partitioning", "Stream source should not be NULL");

	JOIN_ASSERT(((uint64_t) to) % NETWORK_PARTITIONING_CACHELINE_SIZE == 0, "Network Partitioning", "Stream destination not aligned");
	JOIN_ASSERT(((uint64_t) from) % NETWORK_PARTITIONING_CACHELINE_SIZE == 0, "Network Partitioning", "Stream source not aligned");

	register __m256i * d1 = (__m256i *) to;
	register __m256i s1 = *((__m256i *) from);
	register __m256i * d2 = d1 + 1;
	register __m256i s2 = *(((__m256i *) from) + 1);

	_mm256_stream_si256(d1, s1);
	_mm256_stream_si256(d2, s2);

	/*
    register __m128i * d1 = (__m128i*) to;
    register __m128i * d2 = d1+1;
    register __m128i * d3 = d1+2;
    register __m128i * d4 = d1+3;
    register __m128i s1 = *(__m128i*) from;
    register __m128i s2 = *((__m128i*)from + 1);
    register __m128i s3 = *((__m128i*)from + 2);
    register __m128i s4 = *((__m128i*)from + 3);

    _mm_stream_si128 (d1, s1);
    _mm_stream_si128 (d2, s2);
    _mm_stream_si128 (d3, s3);
    _mm_stream_si128 (d4, s4);
    */
}

task_type_t NetworkPartitioning::getType() {
	return TASK_NET_PARTITION;
}

} /* namespace tasks */
} /* namespace hpcjoin */

