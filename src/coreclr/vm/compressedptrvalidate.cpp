// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
//
// Diagnostics for the ARM64 memory optimization work:
//
//   ValidateCompressedPtrInvariant - Stage B0: prove that every address a compressed (32 bit)
//       pointer would have to hold actually lives below 4 GB, before any layout is narrowed.
//   ReportCompressedPtrHeapCensus  - Stage B2 sizing: measure how much narrowing heap-stored
//       object references would actually save on the workload at hand.
//
// See inc/compressedptr.h and arm64-low-va-memory-opt/ for the surrounding design.

#include "common.h"
#include "compressedptr.h"
#include "gcheaputilities.h"
#include "gcdesc.h"
#include <minipal/log.h>

namespace
{
    struct Probe
    {
        const char* name;
        const void* address;
    };

    bool ReportProbe(const Probe& probe, bool* anyViolation)
    {
        bool fits = (probe.address == nullptr) || AddressFitsInCompressedPtr(probe.address);
        if (!fits)
        {
            *anyViolation = true;
        }

        minipal_log_print_info("  %-28s %018p  %s\n",
                               probe.name,
                               probe.address,
                               (probe.address == nullptr) ? "(null)" : (fits ? "ok" : "ABOVE 4GB"));
        return fits;
    }
}

// Checks the addresses that a compressed pointer representation would depend on:
//   - the GC heap range, which bounds every managed object address and therefore every
//     object reference,
//   - well known MethodTable addresses, which live in the loader heaps and are what the
//     MethodTable pointer in each object header holds.
void ValidateCompressedPtrInvariant(const char* phase)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    static ConfigDWORD s_validateCompressedPtr;
    DWORD level = s_validateCompressedPtr.val(CLRConfig::INTERNAL_ValidateCompressedPtr);
    if (level == 0)
    {
        return;
    }

    bool anyViolation = false;

    minipal_log_print_info("ValidateCompressedPtr [%s]: ceiling=0x%zx\n",
                           phase, (size_t)COMPRESSED_PTR_CEILING);

    // Report the MethodTable layout so the per-type saving is observed rather than inferred, and
    // so a mismatch with CoreLib's managed mirror (RuntimeHelpers.CoreCLR.cs) is easy to spot.
    // SIZEOF__MethodTable_ is also the offset at which the vtable starts.
    minipal_log_print_info("  %-28s %u bytes, m_pPerInstInfo@0x%x  %s\n",
                           "MethodTable header",
                           (unsigned)SIZEOF__MethodTable_,
                           (unsigned)offsetof(MethodTable, m_pPerInstInfo),
#ifdef FEATURE_COMPRESSED_MT_FIELDS
                           "(compressed fields)"
#else
                           "(full width fields)"
#endif
                           );

    // GC heap range. Every object address, and hence every object reference, falls inside it.
    // g_lowest_address / g_highest_address are DAC-aware pointers, so go through dac_cast.
    Probe heapProbes[] = {
        { "g_lowest_address",  (const void*)dac_cast<TADDR>(g_lowest_address) },
        { "g_highest_address", (const void*)dac_cast<TADDR>(g_highest_address) },
    };

    for (const Probe& probe : heapProbes)
    {
        ReportProbe(probe, &anyViolation);
    }

    // MethodTable addresses. These come from the loader heaps rather than the GC heap, so they
    // are a separate part of the invariant.
    Probe typeProbes[] = {
        { "MT System.Object",  (const void*)g_pObjectClass },
        { "MT System.String",  (const void*)g_pStringClass },
        { "MT System.Array",   (const void*)g_pArrayClass },
        { "MT System.Int32",   (const void*)CoreLibBinder::GetElementType(ELEMENT_TYPE_I4) },
    };

    for (const Probe& probe : typeProbes)
    {
        ReportProbe(probe, &anyViolation);
    }

    if (anyViolation)
    {
        minipal_log_print_error(
            "ValidateCompressedPtr [%s]: FAILED - at least one address is at or above 4GB, so a "
            "32 bit pointer representation is not usable in this process.\n", phase);

        if (level >= 2)
        {
            EEPOLICY_HANDLE_FATAL_ERROR_WITH_MESSAGE(
                COR_E_EXECUTIONENGINE, W("Compressed pointer invariant violated"));
        }
    }
    else
    {
        minipal_log_print_info("ValidateCompressedPtr [%s]: OK - all probed addresses fit in 32 bits\n",
                               phase);
    }
}

//
// Heap census / Stage B2 sizing.
//
// B1 (narrowing the MethodTable slot in the object header) saves nothing on its own, because the
// 4 bytes come back as alignment padding. All of the projected saving comes from B2 (narrowing
// heap-stored object references), and how much that is depends entirely on how reference-rich the
// workload's live objects are. This walks the live heap and reports the answer for the process at
// hand, so the decision to take on B2's cost rests on data rather than on assumed object mixes.
//
// The projection model is deliberately identical to arm64-low-va-memory-opt/tests/sizemeasure,
// which was validated against GC.GetAllocatedBytesForCurrentThread():
//     B1     = align8(size - 4)                       MethodTable slot 8 -> 4
//     B1+B2  = align8(size - 4 - 4 * referenceSlots)   each reference 8 -> 4
// It assumes MIN_OBJECT_SIZE shrinks along with the header; objects that would fall below today's
// minimum are counted separately so the weight of that assumption is visible.
//
// Not built for the DAC: it walks the live heap of the running process, and CGCDesc's pointer
// counting is itself unavailable under DACCESS_COMPILE.
//

#ifndef DACCESS_COMPILE

namespace
{
    enum CensusBucket
    {
        Bucket_RefArray,
        Bucket_ValueArray,
        Bucket_String,
        Bucket_RefObject,
        Bucket_ValueObject,
        Bucket_Count
    };

    const char* const s_censusBucketNames[Bucket_Count] =
    {
        "arrays of references",
        "arrays of values",
        "strings",
        "objects with references",
        "objects without references",
    };

    struct CensusBucketData
    {
        size_t objects;
        size_t refSlots;
        size_t bytes;
        size_t bytesB1;
        size_t bytesB12;
    };

    struct CensusState
    {
        CensusBucketData buckets[Bucket_Count];
        size_t freeObjects;
        size_t freeBytes;
        // Objects whose B1+B2 size would be below today's MIN_OBJECT_SIZE, i.e. the part of the
        // projection that depends on the minimum object size shrinking too.
        size_t belowMinObjects;
        size_t belowMinBytes;

        // Level 2 only: per reference slot validation of the zero-extension premise.
        bool validateSlots;
        size_t slotsChecked;
        size_t slotsAboveCeiling;
        size_t slotsMisaligned;
        const void* firstBadSlotValue;
    };

    // Level 2: visits one reference slot inside an object. `referenced` is the value stored in the
    // slot, `ppSlot` its address.
    //
    // This checks the premise the compressed reference design rests on: every stored reference has
    // a zero upper 32 bits. On a little endian target that makes a 4 byte access to a full width
    // (8 byte) slot correct, which is what lets reference storage be narrowed one shape at a time
    // instead of everywhere at once. See the design note in arm64-low-va-memory-opt/.
    bool ValidateReferenceSlotCallback(Object* referenced, uint8_t** ppSlot, void* pvContext)
    {
        LIMITED_METHOD_CONTRACT;

        CensusState* pState = (CensusState*)pvContext;
        pState->slotsChecked++;

        if (referenced != nullptr && !AddressFitsInCompressedPtr(referenced))
        {
            if (pState->slotsAboveCeiling == 0)
            {
                pState->firstBadSlotValue = referenced;
            }
            pState->slotsAboveCeiling++;
        }

        if ((((uintptr_t)ppSlot) & (sizeof(uint32_t) - 1)) != 0)
        {
            pState->slotsMisaligned++;
        }

        return true;
    }

    // Object alignment only, without clamping to MIN_OBJECT_SIZE - see the note above.
    size_t AlignCensusSize(size_t size)
    {
        return ALIGN_UP(size, (size_t)TARGET_POINTER_SIZE);
    }

    // Number of reference sized slots stored inside the object. CGCDesc is the GC's own
    // description of exactly those slots, so this counts what B2 would narrow, no more.
    size_t CountReferenceSlots(MethodTable* pMT, size_t size, size_t numComponents)
    {
        if (!pMT->ContainsGCPointers())
        {
            return 0;
        }

        size_t slots = CGCDesc::GetNumPointers(pMT, size, numComponents);

        // GetNumPointers adds the collectible type's LoaderAllocator reference, which is
        // synthesized by the GC rather than stored in the object, so it is not B2's to narrow.
        if (pMT->Collectible() && slots > 0)
        {
            slots--;
        }

        return slots;
    }

    bool CensusWalkCallback(Object* pObj, void* pvContext)
    {
        CONTRACTL
        {
            NOTHROW;
            GC_NOTRIGGER;
            MODE_ANY;
        }
        CONTRACTL_END;

        CensusState* pState = (CensusState*)pvContext;

        // Mark bits may be set on the MethodTable slot during a GC.
        MethodTable* pMT = pObj->GetGCSafeMethodTable();
        size_t rawSize = pObj->GetSize();

        if (pMT == g_pFreeObjectMethodTable)
        {
            pState->freeObjects++;
            pState->freeBytes += AlignCensusSize(rawSize);
            return true;
        }

        size_t numComponents = pMT->HasComponentSize() ? (size_t)pObj->GetNumComponents() : 0;
        size_t refSlots = CountReferenceSlots(pMT, rawSize, numComponents);

        if (pState->validateSlots && pMT->ContainsGCPointersOrCollectible())
        {
            GCHeapUtilities::GetGCHeap()->DiagWalkObject2(pObj, &ValidateReferenceSlotCallback, pState);
        }

        CensusBucket bucket;
        if (pMT->IsString())
        {
            bucket = Bucket_String;
        }
        else if (pMT->IsArray())
        {
            bucket = (refSlots > 0) ? Bucket_RefArray : Bucket_ValueArray;
        }
        else
        {
            bucket = (refSlots > 0) ? Bucket_RefObject : Bucket_ValueObject;
        }

        size_t bytes = AlignCensusSize(rawSize);
        size_t bytesB1 = AlignCensusSize(rawSize - 4);
        size_t bytesB12 = AlignCensusSize(rawSize - 4 - 4 * refSlots);

        CensusBucketData* pData = &pState->buckets[bucket];
        pData->objects++;
        pData->refSlots += refSlots;
        pData->bytes += bytes;
        pData->bytesB1 += bytesB1;
        pData->bytesB12 += bytesB12;

        if (bytesB12 < MIN_OBJECT_SIZE)
        {
            pState->belowMinObjects++;
            pState->belowMinBytes += MIN_OBJECT_SIZE - bytesB12;
        }

        return true;
    }

    void ReportCensusRow(const char* name, const CensusBucketData& data, size_t totalBytes)
    {
        if (data.objects == 0)
        {
            return;
        }

        double share = (totalBytes == 0) ? 0.0 : 100.0 * (double)data.bytes / (double)totalBytes;
        double saveB1 = 100.0 * (double)(data.bytes - data.bytesB1) / (double)data.bytes;
        double saveB12 = 100.0 * (double)(data.bytes - data.bytesB12) / (double)data.bytes;

        minipal_log_print_info("  %-28s %9zu %11zu %5.1f%% %11zu %5.1f%% %11zu %5.1f%%\n",
                               name, data.objects, data.bytes, share,
                               data.bytesB1, saveB1, data.bytesB12, saveB12);
    }
}

// Walks the live heap and reports what B1 / B1+B2 would save on it. Must be called with the EE
// suspended and the heap in a walkable state; GCToEEInterface::DiagGCEnd is such a point.
// No-op unless DOTNET_CompressedPtrHeapCensus is set.
void ReportCompressedPtrHeapCensus(int generation)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    static ConfigDWORD s_compressedPtrHeapCensus;
    DWORD level = s_compressedPtrHeapCensus.val(CLRConfig::INTERNAL_CompressedPtrHeapCensus);
    if (level == 0)
    {
        return;
    }

    // Only after a full collection: gen0/gen1 leave most of the heap unexamined, so the census
    // would describe a fraction of the live set and be misleading.
    IGCHeap* pHeap = GCHeapUtilities::GetGCHeap();
    if (generation != (int)pHeap->GetMaxGeneration())
    {
        return;
    }

    CensusState state;
    memset(&state, 0, sizeof(state));
    state.validateSlots = (level >= 2);

    pHeap->DiagWalkHeap(&CensusWalkCallback, &state, pHeap->GetMaxGeneration(),
                        true /* walk the large object heap */);

    size_t totalObjects = 0;
    size_t totalRefSlots = 0;
    size_t totalBytes = 0;
    size_t totalB1 = 0;
    size_t totalB12 = 0;
    for (const CensusBucketData& data : state.buckets)
    {
        totalObjects += data.objects;
        totalRefSlots += data.refSlots;
        totalBytes += data.bytes;
        totalB1 += data.bytesB1;
        totalB12 += data.bytesB12;
    }

    minipal_log_print_info(
        "\nCompressedPtrHeapCensus [after gen%d GC]: %zu live objects, %zu bytes, "
        "%zu reference slots\n", generation, totalObjects, totalBytes, totalRefSlots);
    minipal_log_print_info(
        "  %-28s %9s %11s %6s %11s %6s %11s %6s\n",
        "shape", "objects", "bytes", "share", "B1", "saved", "B1+B2", "saved");

    for (int i = 0; i < Bucket_Count; i++)
    {
        ReportCensusRow(s_censusBucketNames[i], state.buckets[i], totalBytes);
    }

    if (totalBytes != 0)
    {
        minipal_log_print_info(
            "  %-28s %9zu %11zu %5.1f%% %11zu %5.1f%% %11zu %5.1f%%\n",
            "TOTAL", totalObjects, totalBytes, 100.0,
            totalB1, 100.0 * (double)(totalBytes - totalB1) / (double)totalBytes,
            totalB12, 100.0 * (double)(totalBytes - totalB12) / (double)totalBytes);
    }

    minipal_log_print_info(
        "  free space: %zu objects / %zu bytes (not included above)\n",
        state.freeObjects, state.freeBytes);
    minipal_log_print_info(
        "  %zu objects (%zu bytes of the projected saving) fall below today's MIN_OBJECT_SIZE=%d,\n"
        "  so that much of the B1+B2 column assumes the minimum object size shrinks with the header.\n",
        state.belowMinObjects, state.belowMinBytes, (int)MIN_OBJECT_SIZE);

    if (state.validateSlots)
    {
        if (state.slotsAboveCeiling != 0)
        {
            minipal_log_print_error(
                "  reference slots: %zu checked, **%zu hold a value at or above 4GB** (first: %p), "
                "%zu not 4 byte aligned\n"
                "  FAILED - references cannot be narrowed in this process.\n",
                state.slotsChecked, state.slotsAboveCeiling, state.firstBadSlotValue,
                state.slotsMisaligned);
        }
        else
        {
            minipal_log_print_info(
                "  reference slots: %zu checked, all values fit in 32 bits, %zu not 4 byte aligned\n",
                state.slotsChecked, state.slotsMisaligned);
        }
    }
}

#endif // !DACCESS_COMPILE
