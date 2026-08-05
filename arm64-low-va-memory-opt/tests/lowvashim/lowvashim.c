// Device-side stand-in for `qemu-aarch64 -R 0x100000000`.
//
// On the physical board there is no emulator to confine the address space, and
// personality(ADDR_LIMIT_32BIT) is accepted but ignored by the arm64 kernel (measured - see
// ../vaprobe). What *does* work is placing each reservation explicitly: MAP_FIXED_NOREPLACE at a
// low address succeeds, and a plain low hint is honoured when the range is free.
//
// So this LD_PRELOAD shim interposes mmap and steers every address-agnostic anonymous reservation
// into [LOWVA_BASE, 4 GB). That is enough to satisfy the compressed pointer invariants for the
// regions the runtime allocates itself - the GC heap (I1) and the loader heap (I2) - without
// touching runtime source, so a compressed build can be exercised on real hardware before Phase
// A1a/A1b exist.
//
// What it deliberately does NOT do:
//   * move the executable, the shared objects or the stack (already mapped before we run, and all
//     above 4 GB on this board) - those are not compressed-pointer targets
//   * honour MAP_FIXED requests any differently - the caller demanded an exact address
//   * relocate the code heap (I3): PROT_EXEC reservations are steered too, but that invariant has
//     not been validated, so treat any result that depends on it as unproven
//
// This is a test harness, not a shipping mechanism. The real fix is to place the reservations from
// inside the runtime (Phase A1a for the GC, A1b for the PAL loader/code heap).
//
// Build (from this directory):
//   R=$ROOTFS_DIR; G=$R/usr/lib64/gcc/aarch64-tizen-linux-gnu/14.2.0
//   clang-21 --target=aarch64-linux-gnu --sysroot=$R -B$G -L$G -L$R/usr/lib64 \
//            -O1 -shared -fPIC -o liblowvashim.so lowvashim.c -ldl
//
// Use:
//   LD_PRELOAD=/opt/vatest/liblowvashim.so corerun ...
//   LOWVASHIM_VERBOSE=1     log every steered reservation to stderr
//   LOWVASHIM_BASE=0x10000000
//   LOWVASHIM_ALLOW_HIGH=1  let a reservation that does not fit below 4 GB go high anyway
//
// By default a reservation that cannot be placed low **fails** (MAP_FAILED/ENOMEM) rather than being
// allowed above 4 GB. Falling back would hand a compressed build a pointer it then truncates, which
// shows up as a segfault far from the cause; failing instead surfaces as a clean OutOfMemoryException.
// Measured example: `DOTNET_GCRegionRange=C0000000` (3 GB) does not fit in the ~3.75 GB low window
// alongside everything else - with fallback that segfaulted, without it the runtime reports OOM the
// same way the uncompressed build does.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CEILING ((uintptr_t)0x100000000ULL)
#define DEFAULT_BASE ((uintptr_t)0x10000000ULL) // 256 MB, clear of the usual low mappings
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((uintptr_t)(a) - 1))
#define GRANULARITY ((uintptr_t)0x10000ULL) // 64 KB, matches the runtime's reservation alignment

typedef void* (*mmap_fn)(void*, size_t, int, int, int, off_t);

static mmap_fn   s_realMmap;
static uintptr_t s_next;
static int       s_verbose;
static int       s_allowHigh;

// Written once from the constructor, so no locking is needed for these. s_next is only advanced
// with __atomic ops because mmap can be called from several threads at once.
__attribute__((constructor)) static void lowvashim_init(void)
{
    s_realMmap = (mmap_fn)dlsym(RTLD_NEXT, "mmap");

    const char* base = getenv("LOWVASHIM_BASE");
    uintptr_t   b    = DEFAULT_BASE;
    if (base != NULL && base[0] != '\0')
    {
        uintptr_t parsed = (uintptr_t)strtoull(base, NULL, 0);
        if (parsed >= GRANULARITY && parsed < CEILING)
            b = parsed;
    }
    __atomic_store_n(&s_next, ALIGN_UP(b, GRANULARITY), __ATOMIC_RELAXED);

    const char* v = getenv("LOWVASHIM_VERBOSE");
    s_verbose     = (v != NULL && v[0] == '1');

    const char* ah = getenv("LOWVASHIM_ALLOW_HIGH");
    s_allowHigh    = (ah != NULL && ah[0] == '1');
}

// Only address-agnostic anonymous reservations are steered. A caller that named an address either
// demanded it (MAP_FIXED) or already has a placement policy we should not override.
static int should_steer(void* addr, int flags)
{
    if (addr != NULL)
        return 0;
    if ((flags & MAP_FIXED) != 0)
        return 0;
#ifdef MAP_FIXED_NOREPLACE
    if ((flags & MAP_FIXED_NOREPLACE) != 0)
        return 0;
#endif
    return (flags & MAP_ANONYMOUS) != 0;
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (s_realMmap == NULL)
    {
        // dlsym has not run yet (or failed); fall back to the syscall-less error the caller expects.
        s_realMmap = (mmap_fn)dlsym(RTLD_NEXT, "mmap");
        if (s_realMmap == NULL)
        {
            errno = ENOMEM;
            return MAP_FAILED;
        }
    }

    if (!should_steer(addr, flags) || length == 0)
        return s_realMmap(addr, length, prot, flags, fd, offset);

#ifdef MAP_FIXED_NOREPLACE
    size_t    want = ALIGN_UP(length, GRANULARITY);
    uintptr_t start = __atomic_fetch_add(&s_next, want, __ATOMIC_RELAXED);

    // Walk forward past anything already mapped. One wrap back to the base covers space freed by an
    // earlier munmap; after that give up and let the kernel place it wherever it likes.
    for (int attempt = 0; attempt < 64; attempt++)
    {
        if (start + want > CEILING)
        {
            if (attempt == 0)
            {
                __atomic_store_n(&s_next, ALIGN_UP(DEFAULT_BASE, GRANULARITY), __ATOMIC_RELAXED);
                start = __atomic_fetch_add(&s_next, want, __ATOMIC_RELAXED);
                continue;
            }
            break;
        }

        void* p = s_realMmap((void*)start, length, prot, flags | MAP_FIXED_NOREPLACE, fd, offset);
        if (p != MAP_FAILED)
        {
            if (s_verbose)
                fprintf(stderr, "[lowvashim] %zu KB -> %p\n", length / 1024, p);
            return p;
        }

        start = __atomic_fetch_add(&s_next, want, __ATOMIC_RELAXED);
    }

    if (s_verbose)
        fprintf(stderr, "[lowvashim] no low space for %zu KB, %s\n", length / 1024,
                s_allowHigh ? "falling back above 4GB" : "failing (set LOWVASHIM_ALLOW_HIGH=1 to allow)");

    if (!s_allowHigh)
    {
        // Refuse rather than hand back an address a compressed build would truncate.
        errno = ENOMEM;
        return MAP_FAILED;
    }
#endif // MAP_FIXED_NOREPLACE

    return s_realMmap(addr, length, prot, flags, fd, offset);
}

void* mmap64(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return mmap(addr, length, prot, flags, fd, offset);
}
