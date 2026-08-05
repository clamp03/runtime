// Where does the real kernel put our mappings, and can we force them below 4 GB?
//
// Under QEMU we get the low-VA invariant for free (`qemu-aarch64 -R 0x100000000` confines the whole
// guest address space). On the physical device there is no such lever, so before any compressed
// pointer work can run there we need to know which of these actually place memory below 4 GB:
//
//   1. a plain mmap (what the GC does today)
//   2. a plain mmap with a low address *hint*
//   3. MAP_FIXED_NOREPLACE at a low address (what Phase A1a used)
//   4. personality(ADDR_LIMIT_32BIT) followed by a plain mmap
//
// Build (from the repo root):
//   clang-21 --target=aarch64-linux-gnu \
//     --sysroot=$ROOTFS_DIR --gcc-toolchain=$ROOTFS_DIR/usr \
//     -O1 -static -o vaprobe vaprobe.c
//
// Static so it does not depend on the device's C library version.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <unistd.h>

#define FOUR_GB ((unsigned long long)0x100000000ULL)

// The GC reserves one large contiguous range, so probe at that scale rather than a page.
#define PROBE_SIZE ((size_t)256 * 1024 * 1024)

static const char* verdict(void* p)
{
    if (p == MAP_FAILED)
        return "FAILED";
    return ((unsigned long long)(unsigned long)p < FOUR_GB) ? "below 4GB" : "ABOVE 4GB";
}

static void* try_mmap(const char* what, void* hint, int extra_flags)
{
    void* p = mmap(hint, PROBE_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | extra_flags, -1, 0);
    printf("  %-42s -> %-18p %s\n", what, p, verdict(p));
    if (p != MAP_FAILED)
        munmap(p, PROBE_SIZE);
    return p;
}

// The highest address the process currently touches; if this is already above 4 GB then confining
// only new reservations is not enough - the stack and the loaded images are up there too.
static void report_existing_layout(void)
{
    FILE* f = fopen("/proc/self/maps", "r");
    if (f == NULL)
    {
        printf("  (cannot read /proc/self/maps)\n");
        return;
    }

    char                line[512];
    unsigned long long  highest = 0;
    char                highestLine[512] = {0};
    unsigned long long  aboveCount = 0, totalCount = 0;

    while (fgets(line, sizeof(line), f) != NULL)
    {
        unsigned long long start = 0, end = 0;
        if (sscanf(line, "%llx-%llx", &start, &end) != 2)
            continue;

        totalCount++;
        if (start >= FOUR_GB)
            aboveCount++;
        if (end > highest)
        {
            highest = end;
            strncpy(highestLine, line, sizeof(highestLine) - 1);
        }
    }
    fclose(f);

    size_t n = strlen(highestLine);
    if (n > 0 && highestLine[n - 1] == '\n')
        highestLine[n - 1] = '\0';

    printf("  mappings: %llu total, %llu above 4GB\n", totalCount, aboveCount);
    printf("  highest:  %s\n", highestLine);
}

int main(void)
{
    printf("=== VA probe (page size %ld, probe size %zu MB) ===\n\n",
           sysconf(_SC_PAGESIZE), PROBE_SIZE / (1024 * 1024));

    printf("[existing layout at startup]\n");
    report_existing_layout();

    printf("\n[1] plain mmap - what the GC does today\n");
    try_mmap("mmap(NULL)", NULL, 0);

    printf("\n[2] plain mmap with a low address hint (advisory)\n");
    try_mmap("mmap(hint=0x40000000)", (void*)0x40000000UL, 0);
    try_mmap("mmap(hint=0x10000000)", (void*)0x10000000UL, 0);

    printf("\n[3] MAP_FIXED_NOREPLACE at a low address - Phase A1a's mechanism\n");
#ifdef MAP_FIXED_NOREPLACE
    try_mmap("mmap(0x40000000, FIXED_NOREPLACE)", (void*)0x40000000UL, MAP_FIXED_NOREPLACE);
    try_mmap("mmap(0x80000000, FIXED_NOREPLACE)", (void*)0x80000000UL, MAP_FIXED_NOREPLACE);
    try_mmap("mmap(0xc0000000, FIXED_NOREPLACE)", (void*)0xc0000000UL, MAP_FIXED_NOREPLACE);
#else
    printf("  MAP_FIXED_NOREPLACE not defined in these headers\n");
#endif

    printf("\n[4] personality(ADDR_LIMIT_32BIT) then plain mmap\n");
    int old = personality(0xffffffff);
    if (old == -1)
    {
        printf("  personality() query failed\n");
    }
    else if (personality((unsigned int)old | ADDR_LIMIT_32BIT) == -1)
    {
        printf("  personality(ADDR_LIMIT_32BIT) rejected by the kernel\n");
    }
    else
    {
        printf("  personality set (was 0x%x)\n", old);
        try_mmap("mmap(NULL) under ADDR_LIMIT_32BIT", NULL, 0);
        personality((unsigned int)old);
    }

    printf("\n=== done ===\n");
    return 0;
}
