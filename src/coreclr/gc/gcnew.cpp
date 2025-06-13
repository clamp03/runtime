// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifdef SERVER_GC
#undef SERVER_GC
#endif

#include "common.h"
#include "gcenv.h"

#include "gc.h"
#include "gcscan.h"
#include "gcdesc.h"
#include "gceventstatus.h"

namespace NGC {

#include "gcimpl.h"
#include "gcpriv.h"
IGCHeapInternal* CreateGCHeap() {
    return new(nothrow) GCHeap();
}

class heap_region
{
public:
    heap_region* next;
    size_t curr;
    size_t size;
    size_t count;
    uint8_t mem;

    bool isInHeapRegion(void* addr)
    {
        return (uintptr_t)addr >= (uintptr_t)&mem && (uintptr_t)addr < (uintptr_t)&mem + curr;
    }
    void* getAddr(size_t diff = 0)
    {
        return (void*)(&mem + diff);
    }
    void* getCurrAddr()
    {
        return getAddr(curr);
    }
    void* getEndAddr()
    {
        return getAddr(size);
    }
    size_t getSize()
    {
        return curr;
    }
    size_t getAlloc()
    {
        return size;
    }
};

class ngc_heap
{
public:
    static BOOL init_ngc_heap();
    static BOOL prepare_ngc_thread();
    static void thread_function();
    static void ngc();
    static void ngc_mark_phase();
    static void ngc_copy_phase();
    static void ngc_reloc_phase();
    static void ngc_thread_function(void* args);
    static BOOL create_ngc_thread();
    static BOOL create_ngc_thread_support();
    static void garbage_collect();
    static void start_ngc();
    static BOOL isHeapPointer(void* vpObject, bool small_heap_only);
    static void mark_object_simple(uint8_t** po);
    static uint8_t** findObjectAddress(uint8_t** addr);
    static void relocate_object_simple(uint8_t** po);
    static uint8_t** updateInteriorAddr(uint8_t** intAddr, uint8_t** oldAddr);
    static void mark(Object** ppObject, ScanContext* sc, uint32_t flags);
    static void relocate(Object** ppObject, ScanContext* sc, uint32_t flags);
    static Object* alloc(gc_alloc_context* context, size_t size, uint32_t flags);
    static Object* loh_alloc(gc_alloc_context* context, size_t size, uint32_t flags);
    static uint64_t getTotalAllocatedBytes();
    static size_t getTotalBytesInUse();
    static void* requestObjectCopy(void* addr, bool isInterior);
    static void* updateInteriorObject(void* objAddr, void* addr);
    static void kill_ngc_thread();

public:
    static BOOL keep_ngc_threads_p;
    static BOOL ngc_thread_running;
    static heap_region* MEM;
    static heap_region* OLD_MEM;
    static heap_region* PINNED_MEM;
    static heap_region* LOH_MEM;
    static size_t* IND_LIST;
    static size_t  IND_CURR;
    static CLRCriticalSection ngc_threads_timeout_cs;
    static VOLATILE(BOOL) ngc_started;
    static GCEvent ngc_done_event;
    static size_t gc_count;
    static CFinalize* finalize_queue;
    static uint64_t total_suspended_time;

    //static FinalizerWorkItem* finalizer_work;
};

BOOL ngc_heap::keep_ngc_threads_p = TRUE;
BOOL ngc_heap::ngc_thread_running = FALSE;
heap_region* ngc_heap::MEM = NULL;
heap_region* ngc_heap::OLD_MEM = NULL;
heap_region* ngc_heap::PINNED_MEM = NULL;
heap_region* ngc_heap::LOH_MEM = NULL;
size_t* ngc_heap::IND_LIST = NULL;
size_t  ngc_heap::IND_CURR = 0;
CLRCriticalSection ngc_heap::ngc_threads_timeout_cs;
VOLATILE(BOOL) ngc_heap::ngc_started;
size_t ngc_heap::gc_count = 0;
uint64_t ngc_heap::total_suspended_time = 0;

uint64_t time_clock = 0;
gc_pause_mode pause_mode = pause_interactive;
#ifdef FEATURE_PREMORTEM_FINALIZATION
CFinalize*  ngc_heap::finalize_queue = 0;
static HRESULT AllocateCFinalize(CFinalize **pCFinalize);
#endif // FEATURE_PREMORTEM_FINALIZATION

//FinalizerWorkItem* ngc_heap::finalizer_work = nullptr;

size_t   MEM_SIZE = 1024 * 1024 * 4;
size_t   PINNED_MEM_SIZE = 1024 * 1024 * 10;

uintptr_t g_gc_copying_address = 0;
uint32_t yp_spin_count_unit = 0;
uint32_t original_spin_count_unit = 0;

bool IsInProgress = false;
bool IsSuspensionPending = false;

size_t loh_size_threshold = LARGE_OBJECT_SIZE;

GCEvent ngc_start_event;
GCEvent ngc_heap::ngc_done_event;

GCEvent *GCHeap::WaitForGCEvent         = NULL;

uint64_t qpf;
double qpf_ms;
double qpf_us;

uint64_t GetHighPrecisionTimeStamp()
{
    int64_t ts = GCToOSInterface::QueryPerformanceCounter();

    return (uint64_t)((double)ts * qpf_us);
}

#define SPECIAL_HEADER_BITS (0x3)
#define GC_MARKED       (size_t)0x1
#define OBJ_BIASED      (size_t)0x2

#define MAX_YP_SPIN_COUNT_UNIT 32768

// gcee.cpp
void GCHeap::UpdatePreGCCounters()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::ReportGenerationBounds()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::UpdatePostGCCounters()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

int GCHeap::GetLastGCPercentTimeInGC()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetLastGCGenerationSize(int gen)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetCurrentObjSize()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetLastGCStartTime(int generation)
{
    return (size_t)(time_clock / 1000);
}

size_t GCHeap::GetLastGCDuration(int generation)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetNow()
{
    return (size_t)(GetHighPrecisionTimeStamp() / 1000);
}

bool GCHeap::IsGCInProgressHelper(bool bConsiderGCStart)
{
    return IsInProgress;
}

uint32_t GCHeap::WaitUntilGCComplete(bool bConsiderGCStart)
{
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    if (bConsiderGCStart)
    {
        while (ngc_heap::ngc_started)
        {
            ngc_heap::ngc_done_event.Wait(INFINITE, FALSE);
        }
    }
    uint32_t dwWaitResult = NOERROR;
    if (IsInProgress)
    {
        ASSERT( WaitForGCEvent->IsValid() );
        dwWaitResult = WaitForGCEvent->Wait(INFINITE, FALSE );
    }
    return dwWaitResult;
}

void GCHeap::SetGCInProgress(bool fInProgress)
{
    IsInProgress = fInProgress;
    // assert(!"Not Implemented Yet");
}

void GCHeap::SetWaitForGCEvent()
{
    WaitForGCEvent->Set();
    //assert(!"Not Implemented Yet");
}

void GCHeap::ResetWaitForGCEvent()
{
    WaitForGCEvent->Set();
    // assert(!"Not Implemented Yet");
}

void GCHeap::WaitUntilConcurrentGCComplete()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

bool GCHeap::IsConcurrentGCInProgress()
{
    return IsInProgress;
}

void GCHeap::DiagTraceGCSegments()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagDescrGenerations(gen_walk_fn fn, void *context)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

segment_handle GCHeap::RegisterFrozenSegment(segment_info *pseginfo)
{
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    // assert(!"Not Implemented Yet");
    return NULL;
}

void GCHeap::UnregisterFrozenSegment(segment_handle seg)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

bool GCHeap::IsInFrozenSegment(Object *object)
{
    return false;
}

void GCHeap::UpdateFrozenSegment(segment_handle seg, uint8_t* allocated, uint8_t* committed)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

bool GCHeap::RuntimeStructuresValid()
{
    return GCScan::GetGcRuntimeStructuresValid();
}

void GCHeap::SetSuspensionPending(bool fSuspensionPending)
{
    IsSuspensionPending = fSuspensionPending;
}

void GCHeap::ControlEvents(GCEventKeyword keyword, GCEventLevel level)
{
    GCEventStatus::Set(GCEventProvider_Default, keyword, level);
}

void GCHeap::ControlPrivateEvents(GCEventKeyword keyword, GCEventLevel level)
{
    GCEventStatus::Set(GCEventProvider_Private, keyword, level);
}

uint64_t GCHeap::GetGenerationBudget(int generation)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

// gc.cpp
void GCHeap::Shutdown()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::ValidateObjectMember(Object* obj)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

HRESULT GCHeap::StaticShutdown()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    GCScan::GcRuntimeStructuresValid(FALSE);
    if (ngc_heap::ngc_done_event.IsValid())
    {
        ngc_heap::ngc_done_event.CloseEvent();
    }

#ifdef FEATURE_PREMORTEM_FINALIZATION
    if (ngc_heap::finalize_queue)
    {
        delete ngc_heap::finalize_queue;
    }
#endif // FEATURE_PREMORTEM_FINALIZATION
    return S_OK;
}

HRESULT GCHeap::Init(size_t hn)
{
    return S_OK;
}

HRESULT GCHeap::Initialize()
{
    qpf = (uint64_t)GCToOSInterface::QueryPerformanceFrequency();
    qpf_ms = 1000.0 / (double)qpf;
    qpf_us = 1000.0 * 1000.0 / (double)qpf;
    g_num_processors = GCToOSInterface::GetTotalProcessorCount();
    yp_spin_count_unit = 32 * g_num_processors;
    original_spin_count_unit = yp_spin_count_unit;

    WaitForGCEvent = new (nothrow) GCEvent;
    if (!WaitForGCEvent)
    {
        return E_OUTOFMEMORY;
    }

    if (!WaitForGCEvent->CreateManualEventNoThrow(TRUE))
    {
        return E_FAIL;
    }

    // assert(!"Not Implemented Yet");
    loh_size_threshold = (size_t)GCConfig::GetLOHThreshold();
    loh_size_threshold = max(loh_size_threshold, LARGE_OBJECT_SIZE);

    if (ngc_heap::init_ngc_heap() != TRUE)
    {
        return E_OUTOFMEMORY;
    }

    GCScan::GcRuntimeStructuresValid(TRUE);

    return S_OK;
}

bool GCHeap::IsPromoted(Object* object)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return false;
}

size_t GCHeap::GetPromotedBytes(int heap_index)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}
void GCHeap::SetYieldProcessorScalingFactor(float scalingFactor)
{
    assert (yp_spin_count_unit != 0);
    uint32_t saved_yp_spin_count_unit = yp_spin_count_unit;
    yp_spin_count_unit = (uint32_t)((float)original_spin_count_unit * scalingFactor / (float)9);
    // It's very suspicious if it becomes 0 and also, we don't want to spin too much.
    if ((yp_spin_count_unit == 0) || (yp_spin_count_unit > MAX_YP_SPIN_COUNT_UNIT))
    {
        yp_spin_count_unit = saved_yp_spin_count_unit;
    }
}

unsigned int GCHeap::WhichGeneration(Object* object)
{
    // assert(!"Not Implemented Yet");
    return 0;
}

enable_no_gc_region_callback_status GCHeap::EnableNoGCRegionCallback(NoGCRegionCallbackFinalizerWorkItem* callback, uint64_t callback_threshold)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return enable_no_gc_region_callback_status::not_started;
}

FinalizerWorkItem* GCHeap::GetExtraWorkForFinalization()
{
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    //return Interlocked::ExchangePointer(&ngc_heap::finalizer_work, nullptr);
    return nullptr;
}

unsigned int GCHeap::GetGenerationWithRange(Object* object, uint8_t** ppStart, uint8_t** ppAllocated, uint8_t** ppReserved)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

bool GCHeap::IsEphemeral(Object* object)
{
    return IsHeapPointer((void*)object);
}

Object * GCHeap::NextObj(Object * object)
{
    if (object == nullptr) return nullptr;

    uint8_t* addr = (uint8_t*)object;
    size_t info = *((uintptr_t*)addr + 1);
    size_t size = info & ~0x3;
    if (size == 0) // LOH
    {
        return nullptr;
    }

    Object* ret = nullptr;
    if (g_gc_copying_address == 0)
    {
        Object* nextObj = (Object*)(addr + size);
        if (nextObj->m_pObj != nullptr)
        {
            ret = (Object*)(addr + size);
        }
    }
    else if (ngc_heap::OLD_MEM)
    {
        while (ngc_heap::OLD_MEM->isInHeapRegion(addr))
        {
            addr = addr + size;
            if ((info & GC_MARKED))
            {
                if (((Object*)addr)->m_pObj != nullptr)
                {
                    ret = (Object*)addr;
                }
                break;
            }
            info = *((uintptr_t*)addr + 1);
            size = info & ~0x3;
        }
    }
    return ret;
}

bool GCHeap::IsHeapPointer(void* vpObject, bool small_heap_only)
{
    // assert(!"Not Implemented Yet");
    // // fprintf(stderr, "[CLAMP] GCHeap::IsHeapPointer %p %d\n", vpObject, vpObject >= (void*)MEM && vpObject < (void*)(MEM + MEM_SIZE));
    return ngc_heap::isHeapPointer(vpObject, small_heap_only);
}

void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

#define free_object_base_size (plug_skew + sizeof(ArrayBase))

class CObjectHeader : public Object
{
public:
#if defined(FEATURE_NATIVEAOT) || defined(BUILD_AS_STANDALONE)
    uint32_t GetNumComponents()
    {
        return ((ArrayBase *)this)->GetNumComponents();
    }
#endif //FEATURE_NATIVEAOT || BUILD_AS_STANDALONE

    /////
    //
    // Header Status Information
    //

    MethodTable    *GetMethodTable() const
    {
        return( (MethodTable *) (((size_t) RawGetMethodTable()) & (~SPECIAL_HEADER_BITS)));
    }

    void SetMarked()
    {
        _ASSERTE(RawGetMethodTable());
        RawSetMethodTable((MethodTable *) (((size_t) RawGetMethodTable()) | GC_MARKED));
    }

    BOOL IsMarked() const
    {
        return !!(((size_t)RawGetMethodTable()) & GC_MARKED);
    }

    void SetPinned()
    {
        assert (!(gc_heap::settings.concurrent));
        GetHeader()->SetGCBit();
    }

    BOOL IsPinned() const
    {
        return !!((((CObjectHeader*)this)->GetHeader()->GetBits()) & BIT_SBLK_GC_RESERVE);
    }

    // Now we set more bits should actually only clear the mark bit
    void ClearMarked()
    {
#ifdef DOUBLY_LINKED_FL
        RawSetMethodTable ((MethodTable *)(((size_t) RawGetMethodTable()) & (~GC_MARKED)));
#else
        RawSetMethodTable (GetMethodTable());
#endif //DOUBLY_LINKED_FL
    }

#if 0
#ifdef DOUBLY_LINKED_FL
    void SetBGCMarkBit()
    {
        RawSetMethodTable((MethodTable *) (((size_t) RawGetMethodTable()) | BGC_MARKED_BY_FGC));
    }
    BOOL IsBGCMarkBitSet() const
    {
        return !!(((size_t)RawGetMethodTable()) & BGC_MARKED_BY_FGC);
    }
    void ClearBGCMarkBit()
    {
        RawSetMethodTable((MethodTable *)(((size_t) RawGetMethodTable()) & (~BGC_MARKED_BY_FGC)));
    }

    void SetFreeObjInCompactBit()
    {
        RawSetMethodTable((MethodTable *) (((size_t) RawGetMethodTable()) | MAKE_FREE_OBJ_IN_COMPACT));
    }
    BOOL IsFreeObjInCompactBitSet() const
    {
        return !!(((size_t)RawGetMethodTable()) & MAKE_FREE_OBJ_IN_COMPACT);
    }
    void ClearFreeObjInCompactBit()
    {
#ifdef _DEBUG
        // check this looks like an object, but do NOT validate pointers to other objects
        // as these may not be valid yet - we are calling this during compact_phase
        Validate(FALSE);
#endif //_DEBUG
        RawSetMethodTable((MethodTable *)(((size_t) RawGetMethodTable()) & (~MAKE_FREE_OBJ_IN_COMPACT)));
    }
#endif //DOUBLY_LINKED_FL

    size_t ClearSpecialBits()
    {
        size_t special_bits = ((size_t)RawGetMethodTable()) & SPECIAL_HEADER_BITS;
        if (special_bits != 0)
        {
            assert ((special_bits & (~ALLOWED_SPECIAL_HEADER_BITS)) == 0);
            RawSetMethodTable ((MethodTable*)(((size_t)RawGetMethodTable()) & ~(SPECIAL_HEADER_BITS)));
        }
        return special_bits;
    }

    void SetSpecialBits (size_t special_bits)
    {
        assert ((special_bits & (~ALLOWED_SPECIAL_HEADER_BITS)) == 0);
        if (special_bits != 0)
        {
            RawSetMethodTable ((MethodTable*)(((size_t)RawGetMethodTable()) | special_bits));
        }
    }

    CGCDesc *GetSlotMap ()
    {
        assert (GetMethodTable()->ContainsGCPointers());
        return CGCDesc::GetCGCDescFromMT(GetMethodTable());
    }
#endif

    void SetFree(size_t size)
    {
        assert (size >= free_object_base_size);

        assert (g_gc_pFreeObjectMethodTable->GetBaseSize() == free_object_base_size);
        assert (g_gc_pFreeObjectMethodTable->RawGetComponentSize() == 1);

        RawSetMethodTable( g_gc_pFreeObjectMethodTable );

        size_t* numComponentsPtr = (size_t*) &((uint8_t*)m_pObj)[ArrayBase::GetOffsetOfNumComponents()];
        *numComponentsPtr = size - free_object_base_size;
#ifdef VERIFY_HEAP
        //This introduces a bug in the free list management.
        //((void**) this)[-1] = 0;    // clear the sync block,
        assert (*numComponentsPtr >= 0);
        if (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC)
        {
            memset (((uint8_t*)m_pObj)+sizeof(ArrayBaseInternal), 0xcc, *numComponentsPtr);
#ifdef DOUBLY_LINKED_FL
            // However, in this case we can't leave the Next field uncleared because no one will clear it
            // so it remains 0xcc and that's not good for verification
            if (*numComponentsPtr > 0)
            {
                free_list_slot (m_pObj) = 0;
            }
#endif //DOUBLY_LINKED_FL
        }
#endif //VERIFY_HEAP

#ifdef DOUBLY_LINKED_FL
        // For background GC, we need to distinguish between a free object that's not on the free list
        // and one that is. So we always set its prev to PREV_EMPTY to indicate that it's a free
        // object that's not on the free list. If it should be on the free list, it will be set to the
        // appropriate non zero value.
        check_and_clear_in_free_list ((uint8_t*)m_pObj, size);
#endif //DOUBLY_LINKED_FL
    }

#if 0

    void UnsetFree()
    {
        size_t size = free_object_base_size - plug_skew;

        // since we only need to clear 2 ptr size, we do it manually
        PTR_PTR m = (PTR_PTR) m_pObj;
        for (size_t i = 0; i < size / sizeof(PTR_PTR); i++)
            *(m++) = 0;
    }

    BOOL IsFree () const
    {
        return (GetMethodTable() == g_gc_pFreeObjectMethodTable);
    }
#endif // 0

#ifdef FEATURE_STRUCTALIGN
    int GetRequiredAlignment () const
    {
        return GetMethodTable()->GetRequiredAlignment();
    }
#endif // FEATURE_STRUCTALIGN

    BOOL ContainsGCPointers() const
    {
        return GetMethodTable()->ContainsGCPointers();
    }

#ifdef COLLECTIBLE_CLASS
    BOOL Collectible() const
    {
        return GetMethodTable()->Collectible();
    }

    FORCEINLINE BOOL ContainsGCPointersOrCollectible() const
    {
        MethodTable *pMethodTable = GetMethodTable();
        return (pMethodTable->ContainsGCPointers() || pMethodTable->Collectible());
    }
#endif //COLLECTIBLE_CLASS

    Object* GetObjectBase() const
    {
        return (Object*) this;
    }
};

#define marked(i) header(i)->IsMarked()
#define set_marked(i) header(i)->SetMarked()
#define clear_marked(i) header(i)->ClearMarked()
#define pinned(i) header(i)->IsPinned()
#define set_pinned(i) header(i)->SetPinned()
#define clear_pinned(i) header(i)->GetHeader()->ClrGCBit();

#define ignore_start 0
#define header(i) ((CObjectHeader*)(i))
#define method_table(o) ((CObjectHeader*)(o))->GetMethodTable()
#define get_class_object(i) GCToEEInterface::GetLoaderAllocatorObjectForGC((Object *)i)
inline size_t my_get_size (Object* ob)
{
    MethodTable* mT = header(ob)->GetMethodTable();

    return (mT->GetBaseSize() +
            (mT->HasComponentSize() ?
             ((size_t)((CObjectHeader*)ob)->GetNumComponents() * mT->RawGetComponentSize()) : 0));
}
#define size(i) my_get_size (header(i))

#ifdef COLLECTIBLE_CLASS
#define contain_pointers_or_collectible(i) header(i)->ContainsGCPointersOrCollectible()
#define get_class_object(i) GCToEEInterface::GetLoaderAllocatorObjectForGC((Object *)i)
#define is_collectible(i) method_table(i)->Collectible()
#else //COLLECTIBLE_CLASS
#define contain_pointers_or_collectible(i) header(i)->ContainsGCPointers()
#endif //COLLECTIBLE_CLASS


#define go_through_object(mt,o,size,parm,start,start_useful,limit,exp)      \
{                                                                           \
    CGCDesc* map = CGCDesc::GetCGCDescFromMT((MethodTable*)(mt));           \
    CGCDescSeries* cur = map->GetHighestSeries();                           \
    ptrdiff_t cnt = (ptrdiff_t) map->GetNumSeries();                        \
                                                                            \
    if (cnt >= 0)                                                           \
    {                                                                       \
        CGCDescSeries* last = map->GetLowestSeries();                       \
        uint8_t** parm = 0;                                                 \
        do                                                                  \
        {                                                                   \
            assert (parm <= (uint8_t**)((*(uint8_t**)o) + cur->GetSeriesOffset()));     \
            parm = (uint8_t**)((*(uint8_t**)o) + cur->GetSeriesOffset());               \
            uint8_t** ppstop =                                              \
                (uint8_t**)((uint8_t*)parm + cur->GetSeriesSize() + (size));\
            if (!start_useful || (uint8_t*)ppstop > (start))                \
            {                                                               \
                if (start_useful && (uint8_t*)parm < (start)) parm = (uint8_t**)(start);\
                while (parm < ppstop)                                       \
                {                                                           \
                   {exp}                                                    \
                   parm++;                                                  \
                }                                                           \
            }                                                               \
            cur--;                                                          \
                                                                            \
        } while (cur >= last);                                              \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        /* Handle the repeating case - array of valuetypes */               \
        uint8_t** parm = (uint8_t**)((*(uint8_t**)o) + cur->startoffset);               \
        if (start_useful && start > (uint8_t*)parm)                         \
        {                                                                   \
            ptrdiff_t cs = mt->RawGetComponentSize();                         \
            parm = (uint8_t**)((uint8_t*)parm + (((start) - (uint8_t*)parm)/cs)*cs); \
        }                                                                   \
        while ((uint8_t*)parm < ((*(uint8_t**)o)+(size)-plug_skew))                     \
        {                                                                   \
            for (ptrdiff_t __i = 0; __i > cnt; __i--)                         \
            {                                                               \
                HALF_SIZE_T skip =  (cur->val_serie + __i)->skip;           \
                HALF_SIZE_T nptrs = (cur->val_serie + __i)->nptrs;          \
                uint8_t** ppstop = parm + nptrs;                            \
                if (!start_useful || (uint8_t*)ppstop > (start))            \
                {                                                           \
                    if (start_useful && (uint8_t*)parm < (start)) parm = (uint8_t**)(start);      \
                    do                                                      \
                    {                                                       \
                       {exp}                                                \
                       parm++;                                              \
                    } while (parm < ppstop);                                \
                }                                                           \
                parm = (uint8_t**)((uint8_t*)ppstop + skip);                \
            }                                                               \
        }                                                                   \
    }                                                                       \
}

#define go_through_object_nostart(mt,o,size,parm,exp) {go_through_object(mt,o,size,parm,o,ignore_start,(o + size),exp); }


#ifndef COLLECTIBLE_CLASS
#define go_through_object_cl(mt,o,size,parm,exp)                            \
{                                                                           \
    assert(!"Not Implemented Yet");                                         \
}
#else // COLLECTIBLE_CLASS
#define go_through_object_cl(mt,o,size,parm,exp)                            \
{                                                                           \
    if (header(o)->Collectible())                                           \
    {                                                                       \
        uint8_t* class_obj = get_class_object (o);                             \
        uint8_t** parm = &class_obj;                                           \
        do {exp} while (false);                                             \
    }                                                                       \
    if (header(o)->ContainsGCPointers())                                      \
    {                                                                       \
        go_through_object_nostart(mt,o,size,parm,exp);                      \
    }                                                                       \
}
#endif //COLLECTIBLE_CLASS

void ngc_heap::mark_object_simple(uint8_t** po)
{
    uint8_t* o = *po;
    size_t s = size(o);
    go_through_object_cl(method_table(o), o, s, poo, {
                uint8_t* oo = *poo;
                if (oo != nullptr && !marked(oo))
                {
                    set_marked(oo);
                    if (OLD_MEM->isInHeapRegion((void*)oo))
                    {
                        *((uintptr_t*)oo + 1) |= GC_MARKED;
                    }

                    if (contain_pointers_or_collectible(oo))
                    {
                        mark_object_simple(poo);
                    }
                    clear_marked(oo);
                }
            }
    );
}

uint8_t** ngc_heap::findObjectAddress(uint8_t** addr)
{
    size_t start = 0;
    size_t end = IND_CURR;
    size_t poAddr = (size_t)addr;
    while (start < end)
    {
        size_t mid = (start + end) / 2;

        uint8_t** midAddr = (uint8_t**)OLD_MEM->getAddr(IND_LIST[mid]);
        // fprintf(stderr, "[CLAMP] %s %d %p %p %d %d %d\n", __PRETTY_FUNCTION__, __LINE__, midAddr, addr, mid, start, end);
        if ((uintptr_t)midAddr > (uintptr_t)poAddr)
        {
            end = mid;
        }
        else
        {
            uintptr_t nextAddr = (uintptr_t)(OLD_MEM->getAddr(IND_LIST[mid + 1]));
            // fprintf(stderr, "[CLAMP] %s %d %p %p %p\n", __PRETTY_FUNCTION__, __LINE__, midAddr, addr, (void*)nextAddr);
            if (poAddr < nextAddr)
            {
                // fprintf(stderr, "[CLAMP] %s %d %p %p 0x%x\n", __PRETTY_FUNCTION__, __LINE__, addr, midAddr, nextAddr);
                return midAddr;
#if 0
                if ((uintptr_t)*midAddr >= (uintptr_t)OLD_MEM && (uintptr_t)*midAddr < (uintptr_t)OLD_MEM + OLD_MEM_CURR)
                {
                    return midAddr;
                }
                else
                {
                    uintptr_t addrInfo = *((uintptr_t*)midAddr + 1);
                    uintptr_t newAddr = (uintptr_t)(addrInfo & ~0x3);
                    assert((addrInfo & GC_MARKED) == 0);
                    uintptr_t newRefAddr = (uintptr_t)updateInteriorAddr(addr, midAddr, (uint8_t**)newAddr);
                    Interlocked::Exchange(&g_gc_copying_address, newRefAddr);
                    // fprintf(stderr, "[CLAMP] %s %d %p 0x%x 0x%x\n", __PRETTY_FUNCTION__, __LINE__, midAddr, newAddr, newRefAddr);
                    while (g_gc_copying_address != 0xffffffff)
                    {
                        YieldProcessor();
                    }
                    return nullptr;
                }
#endif
            }
            else
            {
                start = mid + 1;
            }
        }
    }

    // fprintf(stderr, "[CLAMP] %s %d %p %p %p\n", __PRETTY_FUNCTION__, __LINE__, addr, OLD_MEM, OLD_MEM->getCurrAddr());
    assert(!"Cannot find object");
    return nullptr;
}

void ngc_heap::mark(Object** ppObject, ScanContext* sc, uint32_t flags)
{
    uint8_t* po = (uint8_t*)*ppObject;
    if (po == NULL)
    {
        return;
    }

    if (flags & GC_CALL_INTERIOR)
    {
        // TODO Something later
        // fprintf(stderr, "[CLAMP] %s %d MARK INTERIOR %p %p %d\n", __PRETTY_FUNCTION__, __LINE__, ppObject, po, OLD_MEM->isInHeapRegion((void*)po));
        if (OLD_MEM->isInHeapRegion(po))
        {
            po = (uint8_t*)findObjectAddress((uint8_t**)po);
            ppObject = (Object**)&po;
        }
        else
        {
            return;
        }
    }

    if (OLD_MEM->isInHeapRegion((void*)po))
    {
        *((uintptr_t*)po + 1) |= GC_MARKED;
    }

    if (contain_pointers_or_collectible(po))
    {
        ngc_heap::mark_object_simple((uint8_t**)ppObject);
    }
}

void ngc_heap::relocate_object_simple(uint8_t** po)
{
    uint8_t* o = *po;
    size_t s = size(o);
    go_through_object_cl(method_table(o), o, s, poo, {
                uint8_t* oo = *poo;
                if (OLD_MEM->isInHeapRegion((void*)oo))
                {
                    // fprintf(stderr, "[CLAMP] %s %d %p %p %p 0x%x %p\n", __PRETTY_FUNCTION__, __LINE__, (void*)(*((uintptr_t*)oo + 1) & ~0x3), *(uint8_t**)poo, oo, *(uintptr_t*)oo, poo);
                    *poo = (uint8_t*)(*((uintptr_t*)oo + 1) & ~0x3);
                    assert(*poo != (void*)0x0 && *poo != (void*)0x1);
                }
                if (oo != nullptr && !marked(oo))
                {
                    set_marked(oo);
                    if (contain_pointers_or_collectible(oo))
                    {
                        // fprintf(stderr, "[CLAMP] %s %d %p %p 0x%x 0x%x\n", __PRETTY_FUNCTION__, __LINE__, oo, poo, *(uintptr_t*)oo, *(*(uintptr_t**)oo + 1));
                        relocate_object_simple(poo);
                        // fprintf(stderr, "[CLAMP] %s %d %p %p 0x%x 0x%x\n", __PRETTY_FUNCTION__, __LINE__, oo, poo, *(uintptr_t*)oo, *(*(uintptr_t**)oo + 1));
                    }
                    clear_marked(oo);
                }
            }
    );
}

uint8_t** ngc_heap::updateInteriorAddr(uint8_t** intAddr, uint8_t** oldAddr)
{
    uintptr_t addrInfo = *((uintptr_t*)oldAddr + 1);
    uint8_t** newAddr = (uint8_t**)(addrInfo & ~0x3);
    bool biasToggle = (addrInfo & OBJ_BIASED) != 0;
    uint8_t* newObj = *newAddr;

    uintptr_t offset = (uintptr_t)intAddr - (uintptr_t)oldAddr;
    if (biasToggle)
    {
        if ((uintptr_t)newObj - (uintptr_t)newAddr == 12)
        {
            offset -= 4;
        }
        else
        {
            offset += 4;
        }
    }
    return (uint8_t**)((uintptr_t)newAddr + offset);
}


void ngc_heap::relocate(Object** ppObject, ScanContext* sc,
        uint32_t flags)
{
    uint8_t** po = (uint8_t**)*ppObject;
    if (po == NULL)
    {
        return;
    }

    // fprintf(stderr, "[CLAMP] Updated %s %d Check %p to ?? 0x%x\n", __PRETTY_FUNCTION__, __LINE__, *ppObject, flags);
    if (flags & GC_CALL_INTERIOR)
    {
        // fprintf(stderr, "[CLAMP] GC CALL INTERIOR %s %d\n", __PRETTY_FUNCTION__, __LINE__);
        // TODO Something later
        if (!OLD_MEM->isInHeapRegion((void*)po))
        {
            return;
        }

        // fprintf(stderr, "[CLAMP] GC CALL INTERIOR NOW GO!!! %s %d\n", __PRETTY_FUNCTION__, __LINE__);
        uint8_t** obj = findObjectAddress(po);
        *ppObject = (Object*)updateInteriorAddr(po, obj);
        return;
    }

    if (OLD_MEM->isInHeapRegion((void*)po))
    {
        // fprintf(stderr, "[CLAMP] %s %d %p %p\n", __PRETTY_FUNCTION__, __LINE__, *ppObject, (void*)(*((uintptr_t*)po + 1) & ~0x3));
        *ppObject = (Object*)(*((uintptr_t*)po + 1) & ~0x3);
        assert(*ppObject != (void*)0x0 && *ppObject != (void*)0x1);
    }

    if (contain_pointers_or_collectible(po))
    {
        // fprintf(stderr, "[CLAMP] %s %d %p %p\n", __PRETTY_FUNCTION__, __LINE__, ppObject, po);
        relocate_object_simple((uint8_t**)ppObject);
    }
}

/*static*/ bool GCHeap::IsLargeObject(Object *pObj)
{
    return size(pObj) >= loh_size_threshold;
}

bool GCHeap::StressHeap(gc_alloc_context * context)
{
    UNREFERENCED_PARAMETER(context);
    return FALSE;
}

Object* GCHeap::Alloc(gc_alloc_context* context, size_t size, uint32_t flags)
{
    if (size > GetLOHThreshold())
    {
        return ngc_heap::loh_alloc(context, size, flags);
    }
    else
    {
        return ngc_heap::alloc(context, size, flags);
    }
}

void GCHeap::FixAllocContext(gc_alloc_context* context, void* arg, void *heap)
{
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    // assert(!"Not Implemented Yet");
}

Object* GCHeap::GetContainingObject(void *pInteriorPtr, bool fCollectedGenOnly)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return NULL;
}

//////////
BOOL ngc_heap::init_ngc_heap()
{
    assert(MEM == NULL);
    void* allocated = malloc(MEM_SIZE + 16);
    void* pinned_allocated = malloc(PINNED_MEM_SIZE + 16);

    MEM = (heap_region*)memset(allocated, 0, MEM_SIZE + 16);
    PINNED_MEM = (heap_region*)memset(pinned_allocated, 0, PINNED_MEM_SIZE + 16);

    MEM->size = MEM_SIZE;
    PINNED_MEM->size = PINNED_MEM_SIZE;

    MEM->curr = 0;
    PINNED_MEM->curr = 0;

    MEM->next = nullptr;
    PINNED_MEM->next = nullptr;

    assert((uint8_t*)allocated + 16 == (uint8_t*)MEM->getAddr());

    ngc_threads_timeout_cs.Initialize();
    ngc_started = FALSE;

#ifdef FEATURE_PREMORTEM_FINALIZATION
    HRESULT hr = AllocateCFinalize(&finalize_queue);
    if (FAILED(hr))
        return FALSE;
#endif // FEATURE_PREMORTEM_FINALIZATION

    return TRUE;
}

BOOL ngc_heap::prepare_ngc_thread()
{
    BOOL success = FALSE;
    ngc_threads_timeout_cs.Enter();

    if (!ngc_thread_running)
    {
        uint64_t start = GetHighPrecisionTimeStamp();
        GCToEEInterface::SuspendEE(SUSPEND_FOR_GC);
        if (create_ngc_thread_support() && create_ngc_thread())
        {
            ngc_thread_running = TRUE;
            success = TRUE;
        }
        total_suspended_time += GetHighPrecisionTimeStamp() - start;
        GCToEEInterface::RestartEE(TRUE);
    }
    ngc_threads_timeout_cs.Leave();
    return TRUE;
}

void ngc_heap::ngc_mark_phase()
{
    uint64_t start = GetHighPrecisionTimeStamp();
    GCToEEInterface::SuspendEE(SUSPEND_FOR_GC);

    OLD_MEM = MEM;

    int allocSize = MEM_SIZE;
    void* allocated = malloc(allocSize + 16);
    MEM = (heap_region*)memset(allocated, 0, allocSize + 16);
    MEM->size = allocSize;
    MEM->curr = 0;
    MEM->next = OLD_MEM->next;
    OLD_MEM->next = nullptr;
    assert((uint8_t*)allocated + 16 == (uint8_t*)MEM->getAddr());

    ScanContext sc;
    sc.thread_number = 0;
    sc.thread_count = 1;
    sc.promotion = FALSE;
    sc.concurrent = FALSE;
    sc.stack_limit = 0;

    size_t idx = 0;
    IND_CURR = 0;
    IND_LIST = (size_t*)malloc(4 * OLD_MEM->count + 4);
    while (idx < OLD_MEM->curr)
    {
        uint8_t** oldAddr = (uint8_t**)(OLD_MEM->getAddr(idx));
        size_t info = *((uintptr_t*)oldAddr + 1);
        size_t size = info & ~0x3;
        IND_LIST[IND_CURR++] = idx;
        /*
        if (*(uintptr_t*)oldAddr != 0)
        {
            IND_LIST[IND_CURR++] = idx;
        }
        */
        idx += size;
    }
    assert(OLD_MEM->count == IND_CURR);
    IND_LIST[IND_CURR] = OLD_MEM->curr;

    GCScan::GcScanRoots(ngc_heap::mark, 0, 0, &sc);
    GCScan::GcScanHandles(ngc_heap::mark, 0, 0, &sc);
    finalize_queue->CFinalize::GcScanRoots(ngc_heap::mark, 0, &sc);

    Interlocked::Exchange(&g_gc_copying_address, (uintptr_t)0xffffffff);

    total_suspended_time += GetHighPrecisionTimeStamp() - start;
    GCToEEInterface::RestartEE(TRUE);
}

void ngc_heap::ngc_copy_phase()
{
    size_t idx = 0;
    size_t total = 0;
    IND_CURR = 0;
    heap_region* mem = ngc_heap::OLD_MEM;
    while (idx < mem->curr)
    {
        uint8_t** oldAddr = (uint8_t**)(mem->getAddr(idx));
        size_t info = *((uintptr_t*)oldAddr + 1);
        size_t size = info & ~0x3;
        if ((info & GC_MARKED) == GC_MARKED)
        {
            IND_LIST[IND_CURR++] = idx;
        }
        total += 1;
        idx += size;
    }
    IND_LIST[IND_CURR] = mem->curr;

    int i = 0;
    while (i < IND_CURR)
    {
        if ((i % 10) == 0) GCToOSInterface::Sleep(5);

        bool updateChk = false;
        uint8_t** oldAddr = (uint8_t**)VolatileLoad(&g_gc_copying_address);

        if ((uintptr_t)oldAddr != 0xffffffff)
        {
            if (mem->isInHeapRegion((void*)*oldAddr))
            {
                updateChk = true;
            }
            else
            {
                Interlocked::Exchange(&g_gc_copying_address, 0xffffffff);
            }
        }

        if (!updateChk)
        {
            oldAddr = (uint8_t**)(mem->getAddr(IND_LIST[i]));
            i++;
        }
        size_t info = *((uintptr_t*)oldAddr + 1);
        size_t size = info & ~0x3;

        if ((info & GC_MARKED) == 0)
        {
            if (updateChk) Interlocked::Exchange(&g_gc_copying_address, 0xffffffff);
            continue;
        }

        //fprintf(stderr, "[CLAMP] %s %d %d %d %d\n", __PRETTY_FUNCTION__, __LINE__, i, IND_CURR, OLD_MEM->count);
        size_t oldBiased = info & OBJ_BIASED;
        size_t offset = Interlocked::ExchangeAdd(&MEM->curr, size);
        assert(offset + size < MEM->size);
        if (offset + size >= MEM->size)
        {
            int allocSize = MEM_SIZE;
            void* allocated = malloc(allocSize + 16);
            allocated = memset(allocated, 0, allocSize + 16);
        }
        Interlocked::ExchangeAdd(&MEM->count, (size_t)1);

        uint8_t** newAddr = (uint8_t**)MEM->getAddr(offset);
        size_t newBiased = oldBiased;
        uint8_t biasToggle = 0;
        if (((uintptr_t)oldAddr & 0x7) != ((uintptr_t)newAddr & 0x7))
        {
            newBiased = OBJ_BIASED - oldBiased;
            biasToggle = OBJ_BIASED; // 1 for Increase and 2 for Decrease
        }

        uint8_t* newObj = (uint8_t*)((uintptr_t)newAddr + sizeof(ObjHeader) + 4 + 4 + (newBiased ? 4 : 0)); // m_pObj pointer + ObjHeader + info + bias
        *newAddr = newObj;

        memcpy(newObj - 4, *oldAddr - 4, size - 4 - 4);
        *oldAddr = newObj;

        *((uintptr_t*)oldAddr + 1) = (uintptr_t)newAddr | biasToggle;
        *((uintptr_t*)newAddr + 1) = size | newBiased;
        if (updateChk)
        {
            Interlocked::Exchange(&g_gc_copying_address, 0xffffffff);
        }
    }
}

void ngc_heap::ngc_reloc_phase()
{
    ScanContext sc;
    sc.thread_number = 0;
    sc.thread_count = 1;
    sc.promotion = FALSE;
    sc.concurrent = FALSE;
    sc.stack_limit = 0;
    uint64_t start = GetHighPrecisionTimeStamp();
    GCToEEInterface::SuspendEE(SUSPEND_FOR_GC);
    Interlocked::Exchange(&g_gc_copying_address, (uintptr_t)0x0);
    GCScan::GcScanRoots(ngc_heap::relocate, 0, 0, &sc);
    GCScan::GcScanHandles(ngc_heap::relocate, 0, 0, &sc);
    finalize_queue->CFinalize::GcScanRoots(ngc_heap::relocate, 0, &sc);

    free(OLD_MEM);
    OLD_MEM = NULL;

    free(IND_LIST);
    IND_CURR = 0;
    IND_LIST = nullptr;
    total_suspended_time += GetHighPrecisionTimeStamp() - start;
    GCToEEInterface::RestartEE(TRUE);
}

void ngc_heap::garbage_collect()
{
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    bool cooperative_mode = GCToEEInterface::EnablePreemptiveGC();
    prepare_ngc_thread();
    start_ngc();
    time_clock = GetHighPrecisionTimeStamp();
    if (cooperative_mode) GCToEEInterface::DisablePreemptiveGC();
}

void ngc_heap::ngc()
{
    gc_count += 1;
    ngc_started = TRUE;
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    ngc_mark_phase();
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    ngc_copy_phase();
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    ngc_reloc_phase();
    ngc_started = FALSE;
}

BOOL ngc_heap::create_ngc_thread()
{
    ngc_thread_running = GCToEEInterface::CreateThread(ngc_thread_function, NULL, true, ".NET NGC");
    return ngc_thread_running;
}

BOOL ngc_heap::create_ngc_thread_support()
{
    BOOL ret = FALSE;

    if (!ngc_start_event.CreateManualEventNoThrow(FALSE))
    {
        goto cleanup;
    }
    if (!ngc_done_event.CreateManualEventNoThrow(TRUE))
    {
        goto cleanup;
    }
    ret = TRUE;

cleanup:
    if (!ret)
    {
        if (ngc_start_event.IsValid())
        {
            ngc_start_event.CloseEvent();
        }

        if (ngc_done_event.IsValid())
        {
            ngc_done_event.CloseEvent();
        }
    }
    return ret;
}

void ngc_heap::ngc_thread_function(void* args)
{
    bool cooperative_mode = true;
    while (1)
    {
        cooperative_mode = GCToEEInterface::EnablePreemptiveGC();
        uint32_t result = ngc_start_event.Wait(INFINITE, FALSE);
        // fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
        if (!keep_ngc_threads_p)
        {
            ngc_thread_running = FALSE;
            break;
        }
        ngc();

        ngc_start_event.Reset();
        ngc_done_event.Set();
        if (cooperative_mode) GCToEEInterface::DisablePreemptiveGC();
    }
}

void ngc_heap::start_ngc()
{
    assert(ngc_done_event.IsValid());
    assert(ngc_start_event.IsValid());
    ngc_done_event.Wait(INFINITE, FALSE);
    ngc_done_event.Reset();
    ngc_start_event.Set();
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
}


BOOL ngc_heap::isHeapPointer(void* obj, bool small_heap_only)
{
    heap_region* heap = MEM;
    while (heap)
    {
        if (heap->isInHeapRegion(obj))
        {
            return TRUE;
        }
        heap = heap->next;
    }
    if (OLD_MEM)
    {
        if (OLD_MEM->isInHeapRegion(obj)) return TRUE;
    }
    return FALSE;
}

Object* ngc_heap::loh_alloc(gc_alloc_context* context, size_t size, uint32_t flags)
{
    if ((flags & GC_ALLOC_ALIGN8) || (flags & GC_ALLOC_ALIGN8_BIAS))
    {
        size = Align(size) + Align(sizeof(ObjHeader) + 4 + 4);
    }
    else
    {
        size = Align(size) + Align(sizeof(ObjHeader) + 4);
    }

    if ((flags & GC_ALLOC_PINNED_OBJECT_HEAP) == 0)
    {
        size += 4;
    }
    heap_region* allocated = (heap_region*)malloc(size + 16);
    do
    {
        allocated->next = LOH_MEM;
    }
    while(Interlocked::CompareExchange(&LOH_MEM, allocated, allocated->next) != allocated->next);

    allocated->curr = size;
    allocated->size = size;
    allocated->count = 1;
    uint8_t* ret = (uint8_t*)&allocated->mem;
    uint8_t* obj = ret + Align(sizeof(ObjHeader) + 4 + 4); // ObjHeader + m_pObj pointer + next pointer
    uint8_t bias = 0;
    if (flags & GC_ALLOC_ALIGN8 && ((size_t) obj & 7) != 0)
    {
        bias += 4;
    }

    if (flags & GC_ALLOC_ALIGN8_BIAS)
    {
        bias = 4 - bias;
    }
    obj += bias;
    *(uintptr_t*)ret = (uintptr_t)obj;
    *((uintptr_t*)ret + 1) = 0;
    //fprintf(stderr, "[CLAMP] GCHeap::Alloc LOH %p %p Size 0x%zx LOH: %p\n", ret, obj, size, LOH_MEM);
    return (Object*)ret;
}

Object* ngc_heap::alloc(gc_alloc_context* context, size_t size, uint32_t flags)
{
    /*
    static int counter = 0;
    counter += 1;
    if (counter % 100 == 0 && counter < 250)
    {
        //fprintf(stderr, "[CLAMP] %s %d --- \n", __PRETTY_FUNCTION__, __LINE__);
        garbage_collect();
    }
    */
    // fprintf(stderr, "[CLAMP] %s %d ALLOC\n", __PRETTY_FUNCTION__, __LINE__);
    // Check indirection is working well after objects are moved.

    // assert(!"Not Implemented Yet");
    if ((flags & GC_ALLOC_ALIGN8) || (flags & GC_ALLOC_ALIGN8_BIAS))
    {
        size = Align(size) + Align(sizeof(ObjHeader) + 4 + 4);
    }
    else
    {
        size = Align(size) + Align(sizeof(ObjHeader) + 4);
    }
    if ((flags & GC_ALLOC_PINNED_OBJECT_HEAP) == 0)
    {
        size += 4;
    }

    // GarbageCollect(0, (flags & GC_ALLOC_PINNED_OBJECT_HEAP) == 0, (int)size);
    if (flags & GC_ALLOC_PINNED_OBJECT_HEAP)
    {
        // // fprintf(stderr, "[CLAMP] ObjHeader Size %d\n", sizeof(ObjHeader));

        size_t offset = Interlocked::ExchangeAdd(&PINNED_MEM->curr , size);
        assert(offset + size < PINNED_MEM->size);

        Interlocked::ExchangeAdd(&PINNED_MEM->count, (size_t)1);
        uint8_t* ret = (uint8_t*)PINNED_MEM->getAddr(offset);
        uint8_t* obj = ret + Align(sizeof(ObjHeader) + 4 + 4); // ObjHeader + m_pObj pointer + next pointer
        uint8_t bias = 0;
        if (flags & GC_ALLOC_ALIGN8 && ((size_t) obj & 7) != 0)
        {
            bias += 4;
        }

        if (flags & GC_ALLOC_ALIGN8_BIAS)
        {
            bias = 4 - bias;
        }
        obj += bias;
        *(uintptr_t*)ret = (uintptr_t)obj;
        *((uintptr_t*)ret + 1) = size | (bias != 0 ? OBJ_BIASED : 0);
        //fprintf(stderr, "[CLAMP] GCHeap::Alloc PINNED %p %p Size 0x%zx CURR: 0x%zx\n", ret, obj, size, PINNED_MEM->curr - size);

        return (Object*)ret;
    }
    else
    {
        // // fprintf(stderr, "[CLAMP] ObjHeader Size %d\n", sizeof(ObjHeader));
        size_t offset;
        heap_region* mem = MEM;
        while (true)
        {
            offset = Interlocked::ExchangeAdd(&mem->curr, size);
            if (offset + size >= mem->size)
            {
                Interlocked::ExchangeAdd(&mem->curr, -size);
                // fprintf(stderr, "[CLAMP] %s %d ---\n", __PRETTY_FUNCTION__, __LINE__);
                garbage_collect();
            }
            else
            {
                Interlocked::ExchangeAdd(&mem->count, (size_t)1);
                break;
            }
        }

        uint8_t* ret = (uint8_t*)mem->getAddr(offset);
        uint8_t* obj = ret + Align(sizeof(ObjHeader) + 4 + 4); // ObjHeader + m_pObj pointer + next pointer
        uint8_t bias = 0;
        if (flags & GC_ALLOC_ALIGN8 && ((size_t) obj & 7) != 0)
        {
            bias += 4;
        }

        if (flags & GC_ALLOC_ALIGN8_BIAS)
        {
            bias = 4 - bias;
        }
        obj += bias;
        // fprintf(stderr, "[CLAMP] %s %d %p %p 0x%x\n", __PRETTY_FUNCTION__, __LINE__, ret, obj, size);
        *(uintptr_t*)ret = (uintptr_t)obj;
        *((uintptr_t*)ret + 1) = size | (bias != 0 ? OBJ_BIASED : 0);
        //fprintf(stderr, "[CLAMP] GCHeap::Alloc %p %p Size 0x%zx CURR: 0x%zx\n", ret, obj, size, offset);

        return (Object*)ret;
    }
}

uint64_t ngc_heap::getTotalAllocatedBytes()
{
    heap_region* mem = MEM;
    size_t total = PINNED_MEM->getAlloc();
    if (OLD_MEM)
        total += OLD_MEM->getAlloc();
    while (mem)
    {
        total += mem->getAlloc();
        mem = mem->next;
    }
    return total;
}

size_t ngc_heap::getTotalBytesInUse()
{
    heap_region* mem = MEM;
    size_t total = PINNED_MEM->getSize();
    if (OLD_MEM)
        total += OLD_MEM->getSize();
    while (mem)
    {
        total += mem->getSize();
        mem = mem->next;
    }
    return total;

}

void* ngc_heap::requestObjectCopy(void* addr, bool isInterior)
{
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    if (OLD_MEM == nullptr)
    {
        return nullptr;
    }
    if (VolatileLoad(&g_gc_copying_address) == 0 || !OLD_MEM->isInHeapRegion(addr))
    {
        return nullptr;
    }

    uint8_t** obj = (uint8_t**)addr;
    if (isInterior)
    {
        obj = findObjectAddress(obj);
        // fprintf(stderr, "[CLAMP] %s %d %p %p\n", __PRETTY_FUNCTION__, __LINE__, addr, obj);
    }

    if (!OLD_MEM->isInHeapRegion(*obj))
    {
        return nullptr;
    }

    uintptr_t temp = 0;
    do
    {
        temp = Interlocked::CompareExchange(&g_gc_copying_address, (uintptr_t)obj, (uintptr_t)0xffffffff);
        if (temp == 0) return nullptr;
        YieldProcessor();
    } while (temp != 0xffffffff);

    while (VolatileLoad(&g_gc_copying_address) == (uintptr_t)obj)
    {
        YieldProcessor();
    }
    return obj;
}

void* ngc_heap::updateInteriorObject(void* objAddr, void* addr)
{
    //fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    if (VolatileLoad(&g_gc_copying_address) == 0 || !OLD_MEM->isInHeapRegion(addr))
    {
        return addr;
    }

    return (void*)updateInteriorAddr((uint8_t**)addr, (uint8_t**)objAddr);
}

void ngc_heap::kill_ngc_thread()
{
    ngc_threads_timeout_cs.Destroy();
    ngc_start_event.CloseEvent();
    ngc_done_event.CloseEvent();
}

//////////


HRESULT GCHeap::GarbageCollect(int generation, bool isPinned, int size)
{
    ngc_heap::garbage_collect();
    return S_OK;
}

size_t GCHeap::GarbageCollectTry(int generation, BOOL low_memory_p, int mode)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

void* GCHeap::RequestObjectCopy(void* addr, bool isInterior)
{
    return ngc_heap::requestObjectCopy(addr, isInterior);
}

void* GCHeap::UpdateInterioObject(void* objAddr, void* addr)
{
    return ngc_heap::updateInteriorObject(objAddr, addr);
}

unsigned GCHeap::GetGcCount()
{
    return ngc_heap::gc_count;
}

size_t GCHeap::GarbageCollectGeneration(unsigned int gen, gc_reason reason)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetTotalBytesInUse()
{
    return ngc_heap::getTotalBytesInUse();
}

uint64_t GCHeap::GetTotalAllocatedBytes()
{
    return ngc_heap::getTotalAllocatedBytes();
}

int GCHeap::CollectionCount(int generation, int get_bgc_fgc_count)
{
    if (generation == 0 && get_bgc_fgc_count)
    {
        return GetGcCount();
    }
    return 0;
}

size_t GCHeap::ApproxTotalBytesInUse(BOOL small_heap_only)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

bool GCHeap::IsThreadUsingAllocationContextHeap(gc_alloc_context* context, int thread_number)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(thread_number);
    return true;
}

int GCHeap::GetNumberOfHeaps()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::GetHomeHeapNumber()
{
    return 0;
}

unsigned int GCHeap::GetCondemnedGeneration()
{
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
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

int64_t GCHeap::GetTotalPauseDuration()
{
    return (int64_t)(ngc_heap::total_suspended_time * 10);
}

void GCHeap::EnumerateConfigurationValues(void* context, ConfigurationValueFunc configurationValueFunc)
{
    fprintf(stderr, "[CLAMP] %s %d %p %p\n", __PRETTY_FUNCTION__, __LINE__, context, configurationValueFunc);
    GCConfig::EnumerateConfigurationValues(context, configurationValueFunc);
}

uint32_t GCHeap::GetMemoryLoad()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::GetGcLatencyMode()
{
    return pause_mode;
}

int GCHeap::SetGcLatencyMode(int newLatencyMode)
{
    if (pause_mode == pause_no_gc)
    {
        return (int)set_pause_mode_no_gc;
    }
    pause_mode = (gc_pause_mode)newLatencyMode;
    return (int)set_pause_mode_success;
}

int GCHeap::GetLOHCompactionMode()
{
    return loh_compaction_default;
}

void GCHeap::SetLOHCompactionMode(int newLOHCompactionMode)
{
    // NO FEATURE_LOH_COMPACTION
}

bool GCHeap::RegisterForFullGCNotification(uint32_t gen2Percentage,
        uint32_t lohPercentage)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return false;
}

bool GCHeap::CancelFullGCNotification()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return false;
}

int GCHeap::WaitForFullGCApproach(int millisecondsTimeout)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::WaitForFullGCComplete(int millisecondsTimeout)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::StartNoGCRegion(uint64_t totalSize, bool lohSizeKnown, uint64_t lohSize, bool disallowFullBlockingGC)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

int GCHeap::EndNoGCRegion()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::PublishObject(uint8_t* Obj)
{
}

size_t GCHeap::GetValidSegmentSize(bool large_seg)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::SetReservedVMLimit(size_t vmlimit)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

Object* GCHeap::GetNextFinalizableObject()
{
    return ngc_heap::finalize_queue->GetNextFinalizableObject();
}

size_t GCHeap::GetNumberFinalizableObjects()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::GetFinalizablePromotedCount()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

bool GCHeap::RegisterForFinalization(int gen, Object* obj)
{
    if (gen == -1)
        gen = 0;
    if (((((CObjectHeader*)obj)->GetHeader()->GetBits()) & BIT_SBLK_FINALIZER_RUN))
    {
        ((CObjectHeader*)obj)->GetHeader()->ClrBit(BIT_SBLK_FINALIZER_RUN);
        return true;
    }
    else
    {
        return ngc_heap::finalize_queue->RegisterForFinalization (gen, obj);
    }
}

void GCHeap::SetFinalizationRun(Object* obj)
{
    ((CObjectHeader*)obj)->GetHeader()->SetBit(BIT_SBLK_FINALIZER_RUN);
}

void GCHeap::DiagWalkObject(Object* obj, walk_fn fn, void* context)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkObject2(Object* obj, walk_fn2 fn, void* context)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkSurvivorsWithType(void* gc_context, record_surv_fn fn, void* diag_context, walk_surv_type type, int gen_number)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkHeap(walk_fn fn, void* context, int gen_number, bool walk_large_object_heap_p)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkHeapWithACHandling(walk_fn fn, void* context, int gen_number, bool walk_large_object_heap_p)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagWalkFinalizeQueue(void* gc_context, fq_walk_fn fn)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagScanFinalizeQueue(fq_scan_fn fn, ScanContext* sc)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagScanHandles(handle_scan_fn fn, int gen_number, ScanContext* context)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

void GCHeap::DiagScanDependentHandles(handle_scan_fn fn, int gen_number, ScanContext* context)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

size_t GCHeap::GetLOHThreshold()
{
    return loh_size_threshold;
}

void GCHeap::DiagGetGCSettings(EtwGCSettingsInfo* etw_settings)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
}

HRESULT GCHeap::WaitUntilConcurrentGCCompleteAsync(int millisecondsTimeout)
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return S_OK;
}

void GCHeap::TemporaryEnableConcurrentGC()
{
}

void GCHeap::TemporaryDisableConcurrentGC()
{
}

bool GCHeap::IsConcurrentGCEnabled()
{
    return TRUE;
}

int GCHeap::RefreshMemoryLimit()
{
    fprintf(stderr, "[CLAMP] %s %d\n", __PRETTY_FUNCTION__, __LINE__);
    assert(!"Not Implemented Yet");
    return 0;
}

bool CFinalize::Initialize()
{
    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
    } CONTRACTL_END;

    const int INITIAL_FINALIZER_ARRAY_SIZE = 100;
    m_Array = new (nothrow)(Object*[INITIAL_FINALIZER_ARRAY_SIZE]);

    if (!m_Array)
    {
        ASSERT (m_Array);
        STRESS_LOG_OOM_STACK(sizeof(Object*[INITIAL_FINALIZER_ARRAY_SIZE]));
        if (GCConfig::GetBreakOnOOM())
        {
            GCToOSInterface::DebugBreak();
        }
        return false;
    }
    m_EndArray = &m_Array[INITIAL_FINALIZER_ARRAY_SIZE];

    for (int i =0; i < FreeList; i++)
    {
        SegQueueLimit (i) = m_Array;
    }
    //m_PromotedCount = 0;
    lock = -1;
#ifdef _DEBUG
    lockowner_threadid.Clear();
#endif // _DEBUG

    return true;
}

CFinalize::~CFinalize()
{
    delete[] m_Array;
}

Object* CFinalize::GetNextFinalizableObject(BOOL only_non_critical)
{
    Object* obj = 0;
    EnterFinalizeLock();

    if (!IsSegEmpty(FinalizerListSeg))
    {
        obj =  *(--SegQueueLimit (FinalizerListSeg));
    }
    else if (!only_non_critical && !IsSegEmpty(CriticalFinalizerListSeg))
    {
        //the FinalizerList is empty, we can adjust both
        // limit instead of moving the object to the free list
        obj =  *(--SegQueueLimit (CriticalFinalizerListSeg));
        --SegQueueLimit (FinalizerListSeg);
    }
    if (obj)
    {
        dprintf (3, ("running finalizer for %p (mt: %p)", obj, method_table (obj)));
    }
    LeaveFinalizeLock();
    return obj;
}

inline
void CFinalize::EnterFinalizeLock()
{
    _ASSERTE(dbgOnly_IsSpecialEEThread() ||
             GCToEEInterface::GetThread() == 0 ||
             GCToEEInterface::IsPreemptiveGCDisabled());

retry:
    if (Interlocked::CompareExchange(&lock, 0, -1) >= 0)
    {
        unsigned int i = 0;
        while (lock >= 0)
        {
            if (g_num_processors > 1)
            {
                int spin_count = 128 * yp_spin_count_unit;
                for (int j = 0; j < spin_count; j++)
                {
                    if (lock < 0)
                        break;
                    // give the HT neighbor a chance to run
                    YieldProcessor ();
                }
            }
            if (lock < 0)
                break;
            if (++i & 7)
                GCToOSInterface::YieldThread (0);
            else
                GCToOSInterface::Sleep (5);
        }
        goto retry;
    }

#ifdef _DEBUG
    lockowner_threadid.SetToCurrentThread();
#endif // _DEBUG
}

inline
void CFinalize::LeaveFinalizeLock()
{
    _ASSERTE(dbgOnly_IsSpecialEEThread() ||
             GCToEEInterface::GetThread() == 0 ||
             GCToEEInterface::IsPreemptiveGCDisabled());

#ifdef _DEBUG
    lockowner_threadid.Clear();
#endif // _DEBUG
    lock = -1;
}

BOOL
CFinalize::GrowArray()
{
    size_t oldArraySize = (m_EndArray - m_Array);
    size_t newArraySize =  (size_t)(((float)oldArraySize / 10) * 12);

    Object** newArray = new (nothrow) Object*[newArraySize];
    if (!newArray)
    {
        return FALSE;
    }
    memcpy (newArray, m_Array, oldArraySize*sizeof(Object*));

    dprintf (3, ("Grow finalizer array [%p,%p[ -> [%p,%p[", m_Array, m_EndArray, newArray, &m_Array[newArraySize]));

    //adjust the fill pointers
    for (int i = 0; i < FreeList; i++)
    {
        m_FillPointers [i] += (newArray - m_Array);
    }
    delete[] m_Array;
    m_Array = newArray;
    m_EndArray = &m_Array [newArraySize];

    return TRUE;
}

bool
CFinalize::RegisterForFinalization (int gen, Object* obj, size_t size)
{
    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
    } CONTRACTL_END;

    EnterFinalizeLock();

    // Adjust gen
    unsigned int dest = 0; //gen_segment (gen);

    // Adjust boundary for segments so that GC will keep objects alive.
    Object*** s_i = &SegQueue (FreeListSeg);
    if ((*s_i) == SegQueueLimit(FreeListSeg))
    {
        if (!GrowArray())
        {
            LeaveFinalizeLock();
            if (method_table(obj) == NULL)
            {
                // If the object is uninitialized, a valid size should have been passed.
                assert (size >= Align (min_obj_size));
                dprintf (3, (ThreadStressLog::gcMakeUnusedArrayMsg(), (size_t)obj, (size_t)(obj+size)));
                ((CObjectHeader*)obj)->SetFree(size);
            }
            STRESS_LOG_OOM_STACK(0);
            if (GCConfig::GetBreakOnOOM())
            {
                GCToOSInterface::DebugBreak();
            }
            return false;
        }
    }

    Object*** end_si = &SegQueueLimit (dest);
    do
    {
        //is the segment empty?
        if (!(*s_i == *(s_i-1)))
        {
            //no, move the first element of the segment to the (new) last location in the segment
            *(*s_i) = *(*(s_i-1));
        }
        //increment the fill pointer
        (*s_i)++;
        //go to the next segment.
        s_i--;
    } while (s_i > end_si);

    // We have reached the destination segment
    // store the object
    **s_i = obj;
    // increment the fill pointer
    (*s_i)++;

    LeaveFinalizeLock();

    return true;
}

void
CFinalize::GcScanRoots (promote_func* fn, int hn, ScanContext *pSC)
{
    ScanContext sc;
    if (pSC == 0)
        pSC = &sc;

    pSC->thread_number = hn;

    //scan the finalization queue
    Object** startIndex  = SegQueue (FinalizerStartSeg);
    Object** stopIndex  = SegQueueLimit (FinalizerMaxSeg);

    for (Object** po = startIndex; po < stopIndex; po++)
    {
        Object* o = *po;
        //dprintf (3, ("scan freacheable %zx", (size_t)o));
        dprintf (3, ("scan f %zx", (size_t)o));

        (*fn)(po, pSC, 0);
    }
}

#ifdef FEATURE_PREMORTEM_FINALIZATION
static
HRESULT AllocateCFinalize(CFinalize **pCFinalize)
{
    *pCFinalize = new (nothrow) CFinalize();
    if (*pCFinalize == NULL || !(*pCFinalize)->Initialize())
        return E_OUTOFMEMORY;

    return S_OK;
}
#endif // FEATURE_PREMORTEM_FINALIZATION


}
