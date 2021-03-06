#ifndef __GPMR_FIXEDSIZERANGEPARTITIONER_CXX__
#define __GPMR_FIXEDSIZERANGEPARTITIONER_CXX__

#include <gpmr/FixedSizeRangePartitioner.h>
#include <gpmr/EmitConfiguration.h>

#include <mpi.h>

#include <cstring>

namespace gpmr
{

  void gpmrFixedSizeRangePartitionerZeroCountsAndIndices(const int commSize,
                                                         int * gpuBucketCounts,
                                                         int * gpuBucketIndices,
                                                         cudaStream_t * stream);
  void gpmrFixedSizeRangePartitionerCount(const int commSize,
                                          const int numKeys,
                                          int * gpuKeys,
                                          int * gpuBucketCounts,
                                          cudaStream_t * stream);
  void gpmrFixedSizeRangePartitionerPartitionToTemp(const int commSize,
                                                    const int numKeys,
                                                    int * gpuKeys,
                                                    int * gpuVals,
                                                    int * gpuTempKeySpace,
                                                    int * gpuTempValSpace,
                                                    int * gpuKeyOffsets,
                                                    int * gpuBucketIndices,
                                                    cudaStream_t * stream);
  void gpmrFixedSizeRangePartitionerMoveFromTemp(const int commSize,
                                                 const int numKeys,
                                                 int * gpuKeys,
                                                 int * gpuVals,
                                                 int * gpuTempKeySpace,
                                                 int * gpuTempValSpace,
                                                 cudaStream_t * stream);
  void gpmrFixedSizeRangePartitionerSetKeyAndValCounts(const int commSize,
                                                       int * gpuKeyCounts,
                                                       int * gpuValCounts,
                                                       cudaStream_t * stream);
  void gpmrFixedSizeRangePartitionerSetKeyAndValOffsets(const int commSize,
                                                        const int * const gpuKeyCounts,
                                                        int * const gpuKeyOffsets,
                                                        int * const gpuValOffsets,
                                                        cudaStream_t * stream);

  FixedSizeRangePartitioner::FixedSizeRangePartitioner(const int pRangeBegin, const int pRangeEnd)
  {
    rangeBegin = pRangeBegin;
    rangeEnd   = pRangeEnd;
  }
  FixedSizeRangePartitioner::~FixedSizeRangePartitioner()
  {
  }

  bool FixedSizeRangePartitioner::canExecuteOnGPU() const
  {
    return true;
  }

  bool FixedSizeRangePartitioner::canExecuteOnCPU() const
  {
    return false;
  }

  int  FixedSizeRangePartitioner::getMemoryRequirementsOnGPU(gpmr::EmitConfiguration & emitConfig) const
  {
    return emitConfig.getKeySpace() + emitConfig.getValueSpace();
  }

  void FixedSizeRangePartitioner::init()
  {
    MPI_Comm_size(MPI_COMM_WORLD, &commSize);
  }

  void FixedSizeRangePartitioner::finalize()
  {
  }

  void FixedSizeRangePartitioner::executeOnGPUAsync(GPMRGPUConfig gpmrGPUConfig,
                                                    int * gpuKeyOffsets, int * gpuValOffsets,
                                                    int * gpuKeyCounts,  int * gpuValCounts,
                                                    gpmr::EmitConfiguration & emitConfig,
                                                    void * const gpuMemory,
                                                    cudacpp::Stream * kernelStream)
  {
    const int numKeys = gpmrGPUConfig.emitInfo.grid.numThreads * gpmrGPUConfig.emitInfo.grid.emitsPerThread;
    int * gpuTempKeySpace   = reinterpret_cast<int * >(gpuMemory);
    int * gpuTempValSpace   = gpuTempKeySpace + numKeys;
    int * gpuBucketCounts   = gpuKeyCounts;
    int * gpuBucketIndices  = gpuValCounts;
    int * gpuKeys           = reinterpret_cast<int * >(gpmrGPUConfig.keySpace);
    int * gpuVals           = reinterpret_cast<int * >(gpmrGPUConfig.valueSpace);

    gpmrFixedSizeRangePartitionerZeroCountsAndIndices(commSize, gpuBucketCounts, gpuBucketIndices, &kernelStream->getHandle());
    gpmrFixedSizeRangePartitionerCount               (commSize, numKeys, gpuKeys, gpuBucketCounts, &kernelStream->getHandle());
    gpmrFixedSizeRangePartitionerSetKeyAndValOffsets (commSize, gpuKeyCounts, gpuKeyOffsets, gpuValOffsets, &kernelStream->getHandle());
    gpmrFixedSizeRangePartitionerPartitionToTemp     (commSize, numKeys, gpuKeys, gpuVals, gpuTempKeySpace, gpuTempValSpace, gpuKeyOffsets, gpuBucketIndices, &kernelStream->getHandle());
    gpmrFixedSizeRangePartitionerMoveFromTemp        (commSize, numKeys, gpuKeys, gpuVals, gpuTempKeySpace, gpuTempValSpace, &kernelStream->getHandle());
    gpmrFixedSizeRangePartitionerSetKeyAndValCounts  (commSize, gpuKeyCounts, gpuValCounts, &kernelStream->getHandle());
    gpmrFixedSizeRangePartitionerSetKeyAndValOffsets (commSize, gpuKeyCounts, gpuKeyOffsets, gpuValOffsets, &kernelStream->getHandle());
  }

  // even though this isn't supported, we need to add it. otherwise the compiler
  // will give lots of errors.
  void FixedSizeRangePartitioner::executeOnCPUAsync(GPMRCPUConfig gpmrCPUConfig, int * keyOffsets, int * valOffsets)
  {
    const float range = static_cast<float>(rangeEnd - rangeBegin);
    const int numItems = gpmrCPUConfig.emitInfo.grid.numThreads * gpmrCPUConfig.emitInfo.grid.emitsPerThread;
    int * keys = reinterpret_cast<int * >(gpmrCPUConfig.keySpace);
    int * vals = reinterpret_cast<int * >(gpmrCPUConfig.valueSpace);

    for (int i = 0; i < commSize; ++i) keyOffsets[i] = valOffsets[i] = 0;

    int * keyCounts = new int[commSize];
    int * valCounts = new int[commSize];

    memset(keyCounts, 0, sizeof(int) * commSize);
    memset(valCounts, 0, sizeof(int) * commSize);

    for (int i = 0; i < numItems; ++i)
    {
      const float        f     = static_cast<float>(keys[i] - rangeBegin) / range;
      const unsigned int index = static_cast<unsigned int>(f * commSize);
      ++keyCounts[index];
      ++valCounts[index];
    }

    keyOffsets[0] = valOffsets[0] = 0;
    for (int i = 1; i < commSize; ++i)
    {
      keyOffsets[i] = keyOffsets[i - 1] + keyCounts[i - 1];
      valOffsets[i] = valOffsets[i - 1] + valCounts[i - 1];
    }

    int * tempKeys = new int[numItems];
    int * tempVals = new int[numItems];

    memset(keyCounts, 0, sizeof(int) * commSize);
    for (int i = 0; i < numItems; ++i)
    {
      const float        f     = static_cast<float>(keys[i] - rangeBegin) / range;
      const unsigned int index = static_cast<unsigned int>(f * commSize);
      tempKeys[keyOffsets[index] + keyCounts[index]] = keys[i];
      tempVals[keyOffsets[index] + keyCounts[index]] = vals[i];
      keyCounts[index]++;
    }

    memcpy(keys, tempKeys, sizeof(int) * numItems);
    memcpy(vals, tempVals, sizeof(int) * numItems);

    delete [] tempKeys;
    delete [] tempVals;
  }
}

#endif
