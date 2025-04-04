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

uint8_t* MEM = NULL;
size_t   MEM_SIZE = 1024 * 1024 * 10;
size_t   MEM_CURR = 0;
uint8_t* OLD_MEM = NULL;
size_t   OLD_MEM_CURR = 0;

uint8_t* PINNED_MEM = NULL;
size_t   PINNED_MEM_SIZE = 1024 * 1024 * 10;
size_t   PINNED_MEM_CURR = 0;

size_t* IND_DIFF = NULL;
size_t  IND_COUNTER = 0;
ssize_t MEM_DIFF = 0;

bool IsInProgress = false;
bool IsSuspensionPending = false;

bool GC_COLLECTED = false;
bool GC_MOVED = false;

#define SPECIAL_HEADER_BITS (0x3)

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
    return IsInProgress;
}

uint32_t GCHeap::WaitUntilGCComplete(bool bConsiderGCStart)
{
    assert(!"Not Implemented Yet");
    return 0;
}

void GCHeap::SetGCInProgress(bool fInProgress)
{
    IsInProgress = fInProgress;
    // assert(!"Not Implemented Yet");
}

void GCHeap::SetWaitForGCEvent()
{
    //assert(!"Not Implemented Yet");
}

void GCHeap::ResetWaitForGCEvent()
{
    // assert(!"Not Implemented Yet");
}

void GCHeap::WaitUntilConcurrentGCComplete()
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::IsConcurrentGCInProgress()
{
    // assert(!"Not Implemented Yet");
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
    // assert(!"Not Implemented Yet");
    return NULL;
}

void GCHeap::UnregisterFrozenSegment(segment_handle seg)
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::IsInFrozenSegment(Object *object)
{
    // assert(!"Not Implemented Yet");
    return false;
}

void GCHeap::UpdateFrozenSegment(segment_handle seg, uint8_t* allocated, uint8_t* committed)
{
    assert(!"Not Implemented Yet");
}

bool GCHeap::RuntimeStructuresValid()
{
    // assert(!"Not Implemented Yet");
    return true;
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
    // assert(!"Not Implemented Yet");
    return S_OK;
}

HRESULT GCHeap::Initialize()
{
    // assert(!"Not Implemented Yet");
    if (MEM == NULL)
    {
        void* allocated = malloc(MEM_SIZE);
        void* pinned_allocated = malloc(PINNED_MEM_SIZE);
        MEM = (uint8_t*)memset(allocated, 0, MEM_SIZE);
        PINNED_MEM = (uint8_t*)memset(pinned_allocated, 0, PINNED_MEM_SIZE);
        IND_DIFF = (size_t*)malloc(sizeof(size_t) * 1024);
        fprintf(stderr, "[CLAMP] GCHeap::Initialize %p %p %p %p\n",
                MEM, MEM + MEM_SIZE, PINNED_MEM, PINNED_MEM + PINNED_MEM_SIZE);
    }
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
    // assert(!"Not Implemented Yet");
    return 0;
}

enable_no_gc_region_callback_status GCHeap::EnableNoGCRegionCallback(NoGCRegionCallbackFinalizerWorkItem* callback, uint64_t callback_threshold)
{
    assert(!"Not Implemented Yet");
    return enable_no_gc_region_callback_status::not_started;
}

FinalizerWorkItem* GCHeap::GetExtraWorkForFinalization()
{
    // assert(!"Not Implemented Yet");
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
    // assert(!"Not Implemented Yet");
    return NULL;
}

bool GCHeap::IsHeapPointer(void* vpObject, bool small_heap_only)
{
    // assert(!"Not Implemented Yet");
    // fprintf(stderr, "[CLAMP] GCHeap::IsHeapPointer %p %d\n", vpObject, vpObject >= (void*)MEM && vpObject < (void*)(MEM + MEM_SIZE));
    return vpObject >= (void*)MEM && vpObject < (void*)(MEM + MEM_SIZE);
}

void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)
{
    assert(!"Not Implemented Yet");
}

#define GC_MARKED       (size_t)0x1
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

void mark_object_simple(uint8_t** po)
{
    uint8_t* o = *po;
    size_t s = size(o);
    go_through_object_cl(method_table(o), o, s, poo, {
                uint8_t* oo = *poo;
                if (oo != nullptr && !marked(oo))
                {
                    set_marked(oo);
                    if ((uintptr_t)oo >= (uintptr_t)OLD_MEM && (uintptr_t)oo < (uintptr_t)(OLD_MEM + OLD_MEM_CURR))
                    {
                        printf("[CLAMP] mark_object_simple %p %p\n", oo, oo + MEM_DIFF);
                        *(uint8_t**)poo = (oo + MEM_DIFF);
                    }
                    if (oo && contain_pointers_or_collectible(oo))
                    {
                        mark_object_simple(poo);
                    }
                    clear_marked(oo);
                }
            }
    );
}

void GCHeap::Relocate(Object** ppObject, ScanContext* sc,
        uint32_t flags)
{
    uint8_t* o = (uint8_t*)*ppObject;
    if (o == NULL)
    {
        return;
    }

    printf("[CLAMP] Updated %s %d Check %p to %p 0x%x\n", __PRETTY_FUNCTION__, __LINE__, *ppObject, o + MEM_DIFF, flags);
    if ((uintptr_t)o >= (uintptr_t)OLD_MEM && (uintptr_t)o < (uintptr_t)(OLD_MEM + OLD_MEM_CURR))
    {
        *ppObject = (Object*)(o + MEM_DIFF);
    }

    if (flags & GC_CALL_INTERIOR)
    {
        // TODO Something later
    }
    else if (contain_pointers_or_collectible(o))
    {
        mark_object_simple((uint8_t**)ppObject);
    }
}

/*static*/ bool GCHeap::IsLargeObject(Object *pObj)
{
    return false;
}

bool GCHeap::StressHeap(gc_alloc_context * context)
{
    assert(!"Not Implemented Yet");
    return false;
}

Object* GCHeap::Alloc(gc_alloc_context* context, size_t size, uint32_t flags)
{
    printf("[CLAMP] %s %d ALLOC\n", __PRETTY_FUNCTION__, __LINE__);
    // Check indirection is working well after objects are moved.
    GarbageCollect(0, 0, 0);

    // assert(!"Not Implemented Yet");
    if (flags & GC_ALLOC_ALIGN8)
    {
        size = Align(size) + Align(sizeof(ObjHeader) + 4 + 8);
    }
    else
    {
        size = Align(size) + Align(sizeof(ObjHeader) + 4);
    }

    if (flags & GC_ALLOC_PINNED_OBJECT_HEAP)
    {
        assert(PINNED_MEM_CURR + size < PINNED_MEM_SIZE);
        // fprintf(stderr, "[CLAMP] ObjHeader Size %d\n", sizeof(ObjHeader));

        size_t diff = PINNED_MEM_CURR + Align(sizeof(ObjHeader) + 4); // ObjHeader + m_pObj pointer
        uint8_t* ret = ((uint8_t*)PINNED_MEM + diff);
        uint8_t bias = 0;
        if (flags & GC_ALLOC_ALIGN8 && ((size_t) ret & 7) != 0)
        {
            bias += 4;
        }

        if (flags & GC_ALLOC_ALIGN8_BIAS)
        {
            bias = 4 - bias;
        }
        ret += bias;
        *(uintptr_t*)(ret - 8) = (uintptr_t)ret;
        PINNED_MEM_CURR += size;
        printf("[CLAMP] GCHeap::Alloc PINNED %p %p Size 0x%zx CURR: 0x%zx\n", ret - 8, ret, size, PINNED_MEM_CURR);

        return (Object*)(ret - 8);
    }
    else
    {
        assert(MEM_CURR + size < MEM_SIZE);
        // fprintf(stderr, "[CLAMP] ObjHeader Size %d\n", sizeof(ObjHeader));

        size_t diff = MEM_CURR + Align(sizeof(ObjHeader) + 4); // ObjHeader + m_pObj pointer
        uint8_t* ret = ((uint8_t*)MEM + diff);
        uint8_t bias = 0;
        if (flags & GC_ALLOC_ALIGN8 && ((size_t) ret & 7) != 0)
        {
            bias += 4;
        }

        if (flags & GC_ALLOC_ALIGN8_BIAS)
        {
            bias = 4 - bias;
        }
        ret += bias;
        diff += bias;
        *(uintptr_t*)(ret - 8) = (uintptr_t)ret;
        IND_DIFF[IND_COUNTER++] = diff - 8;
        MEM_CURR += size;
        printf("[CLAMP] GCHeap::Alloc %p %p Size 0x%zx CURR: 0x%zx\n", ret - 8, ret, size, MEM_CURR);

        return (Object*)(ret - 8);
    }
}

void GCHeap::FixAllocContext(gc_alloc_context* context, void* arg, void *heap)
{
    // assert(!"Not Implemented Yet");
}

Object* GCHeap::GetContainingObject(void *pInteriorPtr, bool fCollectedGenOnly)
{
    assert(!"Not Implemented Yet");
    return NULL;
}

HRESULT GCHeap::GarbageCollect(int generation, bool low_memory_p, int mode)
{
    if (IND_COUNTER == 0 || IND_COUNTER % 100 != 0)
    {
        return S_OK;
    }

    if (!GC_COLLECTED)
    {
        GC_COLLECTED = true;

        void* allocated = malloc(MEM_SIZE);
        uint8_t* newMem = (uint8_t*)memcpy(allocated, MEM, MEM_CURR);
        MEM_DIFF = (ssize_t)newMem - (ssize_t)MEM;
        printf("[CLAMP] %s %d MEM: %p => %p\n", __PRETTY_FUNCTION__, __LINE__, MEM, newMem);
        for (int i = 0; i < IND_COUNTER; i++)
        {
            size_t diff = IND_DIFF[i];
            uint8_t** oldAddr = (uint8_t**)(MEM + diff);
            uint8_t** newAddr = (uint8_t**)(newMem + diff);

            // Invalidate contents of old object
            uint8_t* oldObj = *oldAddr;
            ((uintptr_t*)oldObj)[0] = 0;
            ((uintptr_t*)oldObj)[1] = 0;

            // Copy indirection data
            *newAddr = oldObj + MEM_DIFF;
            *oldAddr = *newAddr;
            printf("[CLAMP] %s %d OBJ: %p %p %p => %p %p\n", __PRETTY_FUNCTION__, __LINE__, oldAddr, oldObj, *oldAddr, newAddr, *newAddr);
        }
        OLD_MEM = MEM;
        OLD_MEM_CURR = MEM_CURR;
        MEM = newMem;
        printf("[CLAMP] %s %d %d\n", __PRETTY_FUNCTION__, __LINE__, IND_COUNTER);
    }
    else if(!GC_MOVED)
    {
        GC_MOVED = true;

        printf("[CLAMP] %s %d START\n", __PRETTY_FUNCTION__, __LINE__);
        ScanContext sc;
        sc.thread_number = 0;
        sc.thread_count = 1;
        sc.promotion = FALSE;
        sc.concurrent = FALSE;
        sc.stack_limit = 0;
        GCToEEInterface::SuspendEE(SUSPEND_FOR_GC);
        GCScan::GcScanRoots(GCHeap::Relocate, 0, 0, &sc);
        GCScan::GcScanHandles(GCHeap::Relocate, 0, 0, &sc);

        OLD_MEM = (uint8_t*)memset(OLD_MEM, 0, MEM_SIZE);
        free(OLD_MEM);
        GCToEEInterface::RestartEE(TRUE);
        printf("[CLAMP] %s %d DONE\n", __PRETTY_FUNCTION__, __LINE__);
        OLD_MEM = NULL;
        OLD_MEM_CURR = 0;
    }

    return S_OK;
}

size_t GCHeap::GarbageCollectTry(int generation, BOOL low_memory_p, int mode)
{
    assert(!"Not Implemented Yet");
    return 0;
}

unsigned GCHeap::GetGcCount()
{
    // assert(!"Not Implemented Yet");
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
    // assert(!"Not Implemented Yet");
    return 0;
}

size_t GCHeap::ApproxTotalBytesInUse(BOOL small_heap_only)
{
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
    // assert(!"Not Implemented Yet");
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
    //assert(!"Not Implemented Yet");
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
