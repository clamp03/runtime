// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifdef SERVER_GC
#undef SERVER_GC
#endif

#include "common.h"
#include "gcenv.h"
#include "gc.h"

namespace NGC {

#include "gcimpl.h"
IGCHeapInternal* CreateGCHeap() {
    return new(nothrow) GCHeap();
}

// gcee.cpp
void GCHeap::UpdatePreGCCounters()
{
    assert(!"Not Implemented Yet");
}

void GCHeap::ReportGenerationBounds()
{
    assert(!"Not Implemented Yet");
}

void GCHeap::UpdatePostGCCounters()
{
    assert(!"Not Implemented Yet");
}

int GCHeap::GetLastGCPercentTimeInGC()
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetLastGCGenerationSize(int gen)
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetCurrentObjSize()
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetLastGCStartTime(int generation)
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetLastGCDuration(int generation)
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetNow()
{
    assert(!"Not Implemented Yet");
    return 0;
}

bool GCHeap::IsGCInProgressHelper(bool bConsiderGCStart)
{
    assert(!"Not Implemented Yet");
    return false;
}

uint32_t GCHeap::WaitUntilGCComplete(bool bConsiderGCStart)
{
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::SetGCInProgress(bool fInProgress)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::SetWaitForGCEvent()
{
    assert(!"Not Implemented Yet");
}

void GCHeap::ResetWaitForGCEvent()
{
    assert(!"Not Implemented Yet");
}

void GCHeap::WaitUntilConcurrentGCComplete()
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::IsConcurrentGCInProgress()
{
    assert(!"Not Implemented Yet");
    return false;
}

void GCHeap::DiagTraceGCSegments()
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagDescrGenerations(gen_walk_fn fn, void *context)
{
    assert(!"Not Implemented Yet");
}

segment_handle GCHeap::RegisterFrozenSegment(segment_info *pseginfo)
{
    assert(!"Not Implemented Yet");
    return NULL;
}

void GCHeap::UnregisterFrozenSegment(segment_handle seg)
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::IsInFrozenSegment(Object *object)
{
    assert(!"Not Implemented Yet");
    return false;
}

void GCHeap::UpdateFrozenSegment(segment_handle seg, uint8_t* allocated, uint8_t* committed)
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::RuntimeStructuresValid()
{
    assert(!"Not Implemented Yet");
    return false;
}

void GCHeap::SetSuspensionPending(bool fSuspensionPending)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::ControlEvents(GCEventKeyword keyword, GCEventLevel level)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::ControlPrivateEvents(GCEventKeyword keyword, GCEventLevel level)
{
    assert(!"Not Implemented Yet");
}

uint64_t GCHeap::GetGenerationBudget(int generation)
{
    assert(!"Not Implemented Yet");
    return 0;
}

// gc.cpp
void GCHeap::Shutdown()
{
    assert(!"Not Implemented Yet");
}

void GCHeap::ValidateObjectMember(Object* obj)
{
    assert(!"Not Implemented Yet");
}

HRESULT GCHeap::StaticShutdown()
{
    assert(!"Not Implemented Yet");
    return S_OK;
}

HRESULT GCHeap::Init(size_t hn)
{
    assert(!"Not Implemented Yet");
    return S_OK;
}

HRESULT GCHeap::Initialize()
{
    assert(!"Not Implemented Yet");
    return S_OK;
}

bool GCHeap::IsPromoted(Object* object)
{
    assert(!"Not Implemented Yet");
    return false;
}

size_t GCHeap::GetPromotedBytes(int heap_index)
{
    assert(!"Not Implemented Yet");
    return 0;
}
void GCHeap::SetYieldProcessorScalingFactor(float scalingFactor)
{
    assert(!"Not Implemented Yet");
}

unsigned int GCHeap::WhichGeneration(Object* object)
{
    assert(!"Not Implemented Yet");
    return 0;
}

enable_no_gc_region_callback_status GCHeap::EnableNoGCRegionCallback(NoGCRegionCallbackFinalizerWorkItem* callback, uint64_t callback_threshold)
{
    assert(!"Not Implemented Yet");
    return enable_no_gc_region_callback_status::not_started;
}

FinalizerWorkItem* GCHeap::GetExtraWorkForFinalization()
{
    assert(!"Not Implemented Yet");
    return NULL;
}

unsigned int GCHeap::GetGenerationWithRange(Object* object, uint8_t** ppStart, uint8_t** ppAllocated, uint8_t** ppReserved)
{
    assert(!"Not Implemented Yet");
    return 0;
}

bool GCHeap::IsEphemeral(Object* object)
{
    assert(!"Not Implemented Yet");
    return false;
}

Object * GCHeap::NextObj(Object * object)
{
    assert(!"Not Implemented Yet");
    return NULL;
}

bool GCHeap::IsHeapPointer(void* vpObject, bool small_heap_only)
{
    assert(!"Not Implemented Yet");
    return false;
}

void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::Relocate(Object** ppObject, ScanContext* sc,
        uint32_t flags)
{
    assert(!"Not Implemented Yet");
}

/*static*/ bool GCHeap::IsLargeObject(Object *pObj)
{
    assert(!"Not Implemented Yet");
    return false;
}

bool GCHeap::StressHeap(gc_alloc_context * context)
{
    assert(!"Not Implemented Yet");
    return false;
}

Object* GCHeap::Alloc(gc_alloc_context* context, size_t size, uint32_t flags)
{
    assert(!"Not Implemented Yet");
    return NULL;
}

void GCHeap::FixAllocContext(gc_alloc_context* context, void* arg, void *heap)
{
    assert(!"Not Implemented Yet");
}

Object* GCHeap::GetContainingObject(void *pInteriorPtr, bool fCollectedGenOnly)
{
    assert(!"Not Implemented Yet");
    return NULL;
}

HRESULT GCHeap::GarbageCollect(int generation, bool low_memory_p, int mode)
{
    assert(!"Not Implemented Yet");
    return S_OK;
}

size_t GCHeap::GarbageCollectTry(int generation, BOOL low_memory_p, int mode)
{
    assert(!"Not Implemented Yet");
    return 0;
}

unsigned GCHeap::GetGcCount()
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GarbageCollectGeneration(unsigned int gen, gc_reason reason)
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetTotalBytesInUse()
{
    assert(!"Not Implemented Yet");
    return 0;
}

uint64_t GCHeap::GetTotalAllocatedBytes()
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::CollectionCount(int generation, int get_bgc_fgc_count)
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::ApproxTotalBytesInUse(BOOL small_heap_only)
{
    assert(!"Not Implemented Yet");
    return 0;
}

bool GCHeap::IsThreadUsingAllocationContextHeap(gc_alloc_context* context, int thread_number)
{
    assert(!"Not Implemented Yet");
    return false;
}

int GCHeap::GetNumberOfHeaps()
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::GetHomeHeapNumber()
{
    assert(!"Not Implemented Yet");
    return 0;
}

unsigned int GCHeap::GetCondemnedGeneration()
{
    assert(!"Not Implemented Yet");
    return 0;
}
void GCHeap::GetMemoryInfo(uint64_t* highMemLoadThresholdBytes,
        uint64_t* totalAvailableMemoryBytes,
        uint64_t* lastRecordedMemLoadBytes,
        uint64_t* lastRecordedHeapSizeBytes,
        uint64_t* lastRecordedFragmentationBytes,
        uint64_t* totalCommittedBytes,
        uint64_t* promotedBytes,
        uint64_t* pinnedObjectCount,
        uint64_t* finalizationPendingCount,
        uint64_t* index,
        uint32_t* generation,
        uint32_t* pauseTimePct,
        bool* isCompaction,
        bool* isConcurrent,
        uint64_t* genInfoRaw,
        uint64_t* pauseInfoRaw,
        int kind)
{
    assert(!"Not Implemented Yet");
}

int64_t GCHeap::GetTotalPauseDuration()
{
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::EnumerateConfigurationValues(void* context, ConfigurationValueFunc configurationValueFunc)
{
    assert(!"Not Implemented Yet");
}

uint32_t GCHeap::GetMemoryLoad()
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::GetGcLatencyMode()
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::SetGcLatencyMode(int newLatencyMode)
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::GetLOHCompactionMode()
{
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::SetLOHCompactionMode(int newLOHCompactionMode)
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::RegisterForFullGCNotification(uint32_t gen2Percentage,
        uint32_t lohPercentage)
{
    assert(!"Not Implemented Yet");
    return false;
}

bool GCHeap::CancelFullGCNotification()
{
    assert(!"Not Implemented Yet");
    return false;
}

int GCHeap::WaitForFullGCApproach(int millisecondsTimeout)
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::WaitForFullGCComplete(int millisecondsTimeout)
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::StartNoGCRegion(uint64_t totalSize, bool lohSizeKnown, uint64_t lohSize, bool disallowFullBlockingGC)
{
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::EndNoGCRegion()
{
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::PublishObject(uint8_t* Obj)
{
    assert(!"Not Implemented Yet");
}

size_t GCHeap::GetValidSegmentSize(bool large_seg)
{
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::SetReservedVMLimit(size_t vmlimit)
{
    assert(!"Not Implemented Yet");
}

Object* GCHeap::GetNextFinalizableObject()
{
    assert(!"Not Implemented Yet");
    return NULL;
}

size_t GCHeap::GetNumberFinalizableObjects()
{
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetFinalizablePromotedCount()
{
    assert(!"Not Implemented Yet");
    return 0;
}

bool GCHeap::RegisterForFinalization(int gen, Object* obj)
{
    assert(!"Not Implemented Yet");
    return false;
}

void GCHeap::SetFinalizationRun(Object* obj)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkObject(Object* obj, walk_fn fn, void* context)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkObject2(Object* obj, walk_fn2 fn, void* context)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkSurvivorsWithType(void* gc_context, record_surv_fn fn, void* diag_context, walk_surv_type type, int gen_number)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkHeap(walk_fn fn, void* context, int gen_number, bool walk_large_object_heap_p)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkHeapWithACHandling(walk_fn fn, void* context, int gen_number, bool walk_large_object_heap_p)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkFinalizeQueue(void* gc_context, fq_walk_fn fn)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagScanFinalizeQueue(fq_scan_fn fn, ScanContext* sc)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagScanHandles(handle_scan_fn fn, int gen_number, ScanContext* context)
{
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagScanDependentHandles(handle_scan_fn fn, int gen_number, ScanContext* context)
{
    assert(!"Not Implemented Yet");
}

size_t GCHeap::GetLOHThreshold()
{
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::DiagGetGCSettings(EtwGCSettingsInfo* etw_settings)
{
    assert(!"Not Implemented Yet");
}

HRESULT GCHeap::WaitUntilConcurrentGCCompleteAsync(int millisecondsTimeout)
{
    assert(!"Not Implemented Yet");
    return S_OK;
}

void GCHeap::TemporaryEnableConcurrentGC()
{
    assert(!"Not Implemented Yet");
}

void GCHeap::TemporaryDisableConcurrentGC()
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::IsConcurrentGCEnabled()
{
    assert(!"Not Implemented Yet");
    return false;
}

int GCHeap::RefreshMemoryLimit()
{
    assert(!"Not Implemented Yet");
    return 0;
}
}
