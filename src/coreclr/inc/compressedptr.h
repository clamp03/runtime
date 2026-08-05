// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifndef _COMPRESSEDPTR_H_
#define _COMPRESSEDPTR_H_

//
// Compressed (32 bit) pointers.
//
// Stage B of the ARM64 memory optimization work (see arm64-low-va-memory-opt/): on memory
// constrained 64-bit devices the dominant per-object cost is that every managed reference and
// every MethodTable pointer occupies 8 bytes even though the process only ever uses a few
// hundred MB. When every runtime address is guaranteed to live below 4 GB, such a pointer can
// be *stored* in 4 bytes and widened on use, halving the pointer-heavy part of the heap.
//
// The "everything below 4 GB" guarantee is an external precondition, not something this header
// establishes. Today it is produced by running under `qemu-aarch64 -R 0x100000000` (which
// confines the whole guest address space, including glibc malloc arenas). On real hardware it
// requires an equivalent OS-level mechanism.
//
// This header is the Stage B0 foundation: the storage type plus the invariant checker used to
// prove the precondition actually holds before any layout is changed. Nothing here changes
// object layout on its own; layout changes come in later stages behind
// FEATURE_COMPRESSED_POINTERS.
//

#include <stdint.h>

// Addresses at or above this value cannot be represented in a compressed pointer.
#define COMPRESSED_PTR_CEILING ((uintptr_t)0x100000000ULL)

// True when the address can be stored in 32 bits.
inline bool AddressFitsInCompressedPtr(const void* address)
{
    return ((uintptr_t)address >> 32) == 0;
}

// A pointer stored in 32 bits. Only valid while every address it may hold is below
// COMPRESSED_PTR_CEILING; Set() enforces that in checked builds.
//
// Deliberately trivially copyable and free of constructors so it can be embedded in runtime
// data structures whose layout is described to the JIT/DAC by offset.
template <typename T>
class CompressedPtr
{
    uint32_t m_value;

public:
    T* Get() const
    {
        // Zero extension: the base of the compressed range is 0, so no bias is needed.
        return reinterpret_cast<T*>(static_cast<uintptr_t>(m_value));
    }

    void Set(T* value)
    {
        _ASSERTE_MSG(AddressFitsInCompressedPtr(value),
                     "Address does not fit in a compressed pointer; the low-VA precondition is broken");
        m_value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
    }

    bool IsNull() const
    {
        return m_value == 0;
    }

    void SetNull()
    {
        m_value = 0;
    }

    // Raw accessors for the (few) places that need to manipulate the stored form directly,
    // such as the JIT interface and diagnostics.
    uint32_t GetRaw() const { return m_value; }
    void SetRaw(uint32_t raw) { m_value = raw; }

    static size_t StorageSize() { return sizeof(uint32_t); }
};

static_assert(sizeof(CompressedPtr<void>) == 4, "a compressed pointer must occupy 4 bytes");

//
// Invariant checker.
//
// Reports (and optionally asserts on) any runtime address that would not survive being stored
// in a compressed pointer. Enabled with DOTNET_ValidateCompressedPtr:
//   1 - report to stderr
//   2 - report and fail fast
//
// Implemented in vm/compressedptrvalidate.cpp.
//
void ValidateCompressedPtrInvariant(const char* phase);

//
// Live heap census: reports how much narrowing heap stored object references to 4 bytes would
// actually save on the current workload, which is what gates taking on that change. Enabled with
// DOTNET_CompressedPtrHeapCensus=1 and reported after each full GC.
//
// Must be called with the EE suspended and the heap walkable. Implemented in
// vm/compressedptrvalidate.cpp; it walks the live heap of the running process, so it is not
// available to the DAC.
//
#ifndef DACCESS_COMPILE
void ReportCompressedPtrHeapCensus(int generation);
#endif

#endif // _COMPRESSEDPTR_H_
