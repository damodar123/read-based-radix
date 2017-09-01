=============================================
BRIEF EXPLANATION OF THE RADIX JOIN ALGORITHM
=============================================

=======================
Implementation Details:
=======================
	1)Experiments done on Cray supercomputers.
		Cray XC30:
			This has 28 cabinets, each with 3 chassis. Each chassis can hold upto 16 compute blades, which in turn has four compute nodes.
				 This offer up to 5,272 usable compute nodes. 
			Each node is a Single socket, 8 core Intel Xeon E5-2670 machine with 32 GB of main memory.
			They are connected through an Aries routing and communications ASIC,
				and a Dragonfly network topology with a peak network bisection bandwidth of 33 TB/s.
				The Aries ASIC is a system-on-a-chip device comprising four NICs and an Aries router.
			The NICs provide network connectivity to all four nodes of the same blade. Each NIC is connected to the compute node
				by a 16x PCI Express 3 interface. The router is connected to the chassis back plane and through it to the network fabric.

		Cray XC40:
			It has the same architecture as the XC30 but differs in the node design: each compute node has two
				18-core (Intel Xeon E5-2695 v4) processors and 64 GB of main memory per node.


	2) Used MPI versions:
		* openMPI 1.10.1 and 1.20.2
		* foMPI 0.2.1 (Fast One-sided MPI, optimised for Cray XC30 and XC40 systems)
			(https://spcl.inf.ethz.ch/Research/Parallel_Programming/foMPI/)


	3) Experiments scaled upto 4096 cores.

	4) Each tuple is 16-byte, containing an 8-byte key and an 8-byte record id.
		Each process served upto 40million tuples per relation.
		So, 16*40m*2*4096 Bytes =~ 4.8 TB.

 
===============
Article format: 
===============
Name_of_the_Phase // LOG INFO: (code_variable, Terminology of Perf file, Terminology of Main Result log file)
{
    start_timer("Results-Label");

    Brief explanation of the steps involved in this phase
    Sub-phase
    {
        Brief explanation of the steps involved in this sub-phase
    }

    stop_timer("Results-Label");

} Eof Phase

===============================
Start of Algorithm explanation:
===============================

Getting ready:
    Each Process allocates a pool of memory:
	    (innerRelation+outerRelation)*BytesizeOfTuple. = (200000 + 200000)*16
    innerRelation->fillUniqueValues()
    outerRelation->fillUniqueValues()
    innerRelation->distribute(nodeId, numberOfNodes)

HashJoin::Join() 	//LOG INFO: totalTime, JTOTAL, [RESULTS] Join:
{
	start_timer("Join"); //measures on the Total Join time	

	ComputeHistogram  //LOG INFO: phaseTimes[0], JHIST, [RESULTS] Histogram:
	{
		start_timer("Histogram");

		Compute local & global histogram for both Inner and Outer Relation
		Assignment of partition to nodes, computing offsets

		stop_timer("Histogram");

	} //Eof ComputeHistogram
	

	WindowAllocation //LOG INFO: specialTimes[0],SWINALLOC, [RESULTS] WinAlloc:
	{
		start_timer("WinAlloc");

		Seperate windows for inner and outer relation.
        (MPI_Win_create(this->data,localWindowSize * sizeof(hpcjoin::data::CompressedTuple), 1,MPI_INFO_NULL, MPI_COMM_WORLD, window);)


		stop_timer("WinAlloc");

	} //Eof  WindowAllocation


	NetworkPartitioning //LOG INFO: phaseTimes[1], JMPI, [RESULTS] Network:
	{
		start_timer("Network");
		MPI_Win_lock_all(0, *window);

		for each of the 2(inner, outer) relations
		{
			Create inMemoryBuffer: (Each buffer is 64KB, i.e Cachelines_per_Buffer * cacheline_size_bytes, and we have 2 buffers per partition)
				inMemoryBufferSize  =   Mem_buffers_per_partion * PARTITIONING_COUNT * Cachelines_per_Buffer * cacheline_size_bytes 
									=	2 * 1024 * 1024 * 64
									=	128 MB
			Create in-cache buffer
				cacheline_t inCacheBuffer[PARTITIONING_COUNT] //array of 1024 elements of each 8 Bytes (used to hold inCacheCounters and inMemoryCounters of each partion)

			for each element of the relation
			{
				Compute the partition to which this element belongs to:
					partitionId = HASH_BIT_MODULO(data[i].key, ((1<< 10) - 1), 0);
				Move data to cache line:
					CompressedTuple *cacheLine = (hpcjoin::data::CompressedTuple *) (inCacheBuffer + partitionId);
					(CompressedTuple is a structure with single element "value" of size 8byte)
					cacheLine[inCacheCounter].value = data[i].rid + ((data[i].key >> partitionBits) << (partitionBits + hpcjoin::core::Configuration::PAYLOAD_BITS));

				Move cache line to memory buffer:
					get the address of the memorybuffer related to this partition:
						char *inMemoryStreamDestination = PARTITION_ACCESS(partitionId) + (memoryCounter * NETWORK_PARTITIONING_CACHELINE_SIZE);
					streamWrite(inMemoryStreamDestination, cacheLine);
						uses AVX instruction, _mm256_stream_si256, to transfer the data from cacheline to Memory buffer.
					++memoryCounter

				Check if memory buffer is full:
					if (memoryCounter % CACHELINES_PER_MEMORY_BUFFER == 0)
					{
						compute the target process ID, write offset of window based on partition Id.
						sizeInTuples = CACHELINES_PER_MEMORY_BUFFER * TUPLES_PER_CACHELINE; //which is 1024 * 1
						MPI_Put(inMemoryBufferLocation, sizeInTuples * sizeof(CompressedTuple), MPI_BYTE, targetProcess, 
									targetOffset * sizeof(CompressedTuple), sizeInTuples * sizeof(CompressedTuple), MPI_BYTE, *window);

						If we used both the buffers allocated for a partition, then wait for the RMA operations belongs to that particular partition and reuse the buffers:
							if(memoryCounter == MEMORY_BUFFERS_PER_PARTITION * CACHELINES_PER_MEMORY_BUFFER)
							{ 
								foMPI_Win_flush_local(target_process, *window)
								memoryCounter = 0;
							}
					}
			}
			Flush remaing partitions
			{
				for each of the partitions:
				{
					calculate the remainingTupleInMemory in this partition buffer.
					compute the target process ID, write offset of window based on partition Id.
					MPI_Put(inMemoryBufferOfPartition, remainingTupleInMemory*sizeof(CompressedTuple), MPI_BYTE, targetProcess,
								targetOffset * sizeof(CompressedTuple),remainingTupleInMemory*sizeof(CompressedTuple), MPI_BYTE, *window)
				}
			}
		}
		MPI_Win_flush_local_all(*window);
		MPI_Win_unlock_all(0, *window);

		stop_timer("Network");
			
	} //Eof NetworkPartitioning

	Synchronization  //LOG INFO: specialTimes[1], SNETCOMPL,[RESULTS] PartWait
	{
	    start_timer("PartWait");

		Barrier(); Each Process wait for the writes to be completed to its window.
		
   	    stop_timer("PartWait");

	}//Eof Synchronization
	

	PrepareForLocalProcessing //LOG INFO: specialTimes[2], SLOCPREP, [RESULTS] LocalPrep:
	{
	    start_timer("LocalPrep");

		for every partition assigned to this node
			TASK_QUEUE.push(new hpcjoin::tasks::LocalPartitioning(innerRelationPartitionSize, innerRelationPartition, outerRelationPartitionSize, outerRelationPartition))

	    stop_timer("LocalPrep");
	
	}//Eof PrepareForLocalProcessing


	LocalProcessing // LOG INFO: phaseTimes[2],JPROC,[RESULTS] Local:
	{
	    start_timer("Local");

		for every task in  TASK_QUEUE
			localPartitioning->execute() //LOG INFO: localPartitioningTaskTimeSum=LPTASKTIME = [RESULTS] LocalPart:
			{
			    	start_timer("LocalPart");

				compute histogram for both inner and outer relation;
				calculate offsets;
				partition inner relation data locally
				partition outer relation data locally
				for every local_partition
					TASK_QUEUE.push(new hpcjoin::tasks::BuildProbe(innerHistogram[p], innerPartitions+innerOffsets[p], outerHistogram[p],outerPartitions+outerOffsets[p]))

			    stop_timer("LocalPart");

			} //Eof localPartitioning → execute() 
		
		for every task in  TASK_QUEUE
			BuildProbe → execute // LOG INFO: buildProbeTaskTimeSum, BPTASKTIME,[RESULTS] LocalBP:
			{
				start_timer("LocalBP");

				Build Hash table for inner relation//LOG INFO: buildProbeBuildTimeSum, BPBUILD, Nill
				Probe outer relation onto inner relation  //LOG INFO: buildProbeProbeTimeSum, BPPROBE, Nill

				stop_timer("LocalBP");

			} //Eof BuildProbe → execute

	    stop_timer("Local");

	} //Eof LocalProcessing

	stop_timer("Join");

}//End of HashJoin::Join()

============================
End of Algorithm explanation
============================

