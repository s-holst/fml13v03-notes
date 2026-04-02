// SPDX-License-Identifier: GPL-2.0
/*
 * test_dma_memcp.c — test es_dma_memcp via /dev/malloc_dmabuf + /dev/es_memcp
 *
 * gcc -O2 -o test_dma_memcp test_dma_memcp.c
 *
 * Usage: ./test_dma_memcp [size_bytes]
 *   default sizes tested: 4K, 64K, 1M, 16M
 *
 * Diagnostics printed:
 *   - IRQ count delta from /proc/interrupts for the AON DMA IRQ (line 289)
 *   - ESW_CMDQ_QUERY status before and after each transfer
 *   - Transfer timing
 *   - Data validation (pattern check)
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* ---- uapi structs (inlined to avoid kernel header dependency) ----------- */

struct esw_memcp_f2f_cmd {
    int          src_fd;
    unsigned int src_offset;
    int          dst_fd;
    unsigned int dst_offset;
    size_t       len;
    int          timeout;
};

struct esw_cmdq_query {
    int status;
    int task_count;
    int last_error;
};

struct esw_userptr {
    unsigned long      user_ptr;
    unsigned long long length;
    unsigned long long aligned_length;
    unsigned int       offset_in_first_page;
    int                dma_fd;
    unsigned long      fd_flags;
    /* bool is _Bool in C, size may vary — use uint8_t to match kernel ABI */
    uint8_t            mem_type;
};

#define ESW_MEMCP_MAGIC        'M'
#define ESW_CMDQ_ADD_TASK      _IOW(ESW_MEMCP_MAGIC, 1, struct esw_memcp_f2f_cmd)
#define ESW_CMDQ_SYNC          _IO(ESW_MEMCP_MAGIC, 2)
#define ESW_CMDQ_QUERY         _IOR(ESW_MEMCP_MAGIC, 3, struct esw_cmdq_query)

#define ESW_MALLOC_DMABUF_MAGIC    'N'
#define ESW_CONVERT_USERPTR_TO_DMABUF \
    _IOWR(ESW_MALLOC_DMABUF_MAGIC, 1, struct esw_userptr)

/* ---- helpers ------------------------------------------------------------ */

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s\n", strerror(errno));
    exit(1);
}

static double elapsed_ms(struct timespec *start, struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1e3 +
           (end->tv_nsec - start->tv_nsec) / 1e6;
}

/* Read the IRQ count for a given IRQ number from /proc/interrupts.
 * Returns sum across all CPUs, or -1 if not found. */
static long long read_irq_count(int irq)
{
    FILE *f = fopen("/proc/interrupts", "r");
    if (!f)
        return -1;

    char line[1024];
    char needle[16];
    snprintf(needle, sizeof(needle), "%d:", irq);

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ') p++;
        if (strncmp(p, needle, strlen(needle)) != 0)
            continue;
        p += strlen(needle);
        long long total = 0;
        char *end;
        while (1) {
            long long v = strtoll(p, &end, 10);
            if (end == p)
                break;
            total += v;
            p = end;
        }
        fclose(f);
        return total;
    }
    fclose(f);
    return -1;
}

/* Convert a malloc'd buffer to a dma-buf fd via /dev/malloc_dmabuf */
static int buf_to_dmabuf(int mdbfd, void *ptr, size_t len)
{
    struct esw_userptr u = {
        .user_ptr      = (unsigned long)ptr,
        .length        = len,
        .aligned_length = len,
        .mem_type      = 1,   /* malloc */
    };

    if (ioctl(mdbfd, ESW_CONVERT_USERPTR_TO_DMABUF, &u) < 0)
        die("ESW_CONVERT_USERPTR_TO_DMABUF");

    return u.dma_fd;
}

/* DMA_BUF_IOCTL_SYNC wrapper */
static void dmabuf_sync(int fd, uint64_t flags)
{
    struct dma_buf_sync sync = { .flags = flags };
    /* Best-effort: some kernels may not support this for these buffers */
    ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

/* Fill buffer with deterministic pattern: byte[i] = (i ^ seed) & 0xFF */
static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(i ^ seed);
}

/* Validate dst matches src pattern. Returns number of mismatches. */
static size_t validate(const uint8_t *src, const uint8_t *dst, size_t len)
{
    size_t bad = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] != dst[i]) {
            if (bad < 8)
                fprintf(stderr, "  mismatch @%zu: src=0x%02x dst=0x%02x\n",
                        i, src[i], dst[i]);
            bad++;
        }
    }
    return bad;
}

static void print_query(int memcp_fd, const char *label)
{
    struct esw_cmdq_query q = {};
    if (ioctl(memcp_fd, ESW_CMDQ_QUERY, &q) < 0) {
        fprintf(stderr, "  QUERY (%s): failed: %s\n", label, strerror(errno));
        return;
    }
    printf("  QUERY (%s): status=%d task_count=%d last_error=%d\n",
           label, q.status, q.task_count, q.last_error);
}

/* ---- single transfer test ----------------------------------------------- */

static bool run_test(int mdbfd, int memcp_fd, size_t size, int aon_irq)
{
    printf("\n=== size=%zu (%.1f KB) ===\n", size, size / 1024.0);

    /* Allocate page-aligned buffers */
    void *src = NULL, *dst = NULL;
    long page = sysconf(_SC_PAGESIZE);
    if (posix_memalign(&src, page, size) || posix_memalign(&dst, page, size))
        die("posix_memalign");

    fill_pattern(src, size, 0xA5);
    memset(dst, 0, size);

    /* Lock pages so they cannot be swapped before DMA */
    mlock(src, size);
    mlock(dst, size);

    int src_fd = buf_to_dmabuf(mdbfd, src, size);
    int dst_fd = buf_to_dmabuf(mdbfd, dst, size);

    print_query(memcp_fd, "before");

    long long irq_before = read_irq_count(aon_irq);

    /* Sync dst to device before DMA writes into it */
    dmabuf_sync(dst_fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);

    struct esw_memcp_f2f_cmd cmd = {
        .src_fd     = src_fd,
        .src_offset = 0,
        .dst_fd     = dst_fd,
        .dst_offset = 0,
        .len        = size,
        .timeout    = 6000,   /* 6 s — matches driver default */
    };

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    if (ioctl(memcp_fd, ESW_CMDQ_ADD_TASK, &cmd) < 0)
        die("ESW_CMDQ_ADD_TASK");

    if (ioctl(memcp_fd, ESW_CMDQ_SYNC, NULL) < 0)
        die("ESW_CMDQ_SYNC");

    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* Sync dst back to CPU */
    dmabuf_sync(dst_fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);

    long long irq_after = read_irq_count(aon_irq);

    print_query(memcp_fd, "after");

    double ms = elapsed_ms(&t0, &t1);
    double bw = (irq_after >= 0 && irq_before >= 0)
                ? 0 : 0;  /* placeholder */

    printf("  elapsed: %.2f ms", ms);
    if (size > 0 && ms > 0)
        printf("  (%.1f MB/s)", (double)size / ms / 1024.0);
    printf("\n");

    if (aon_irq >= 0 && irq_before >= 0)
        printf("  IRQ %d count delta: %lld\n",
               aon_irq, irq_after - irq_before);
    else
        printf("  IRQ count: unavailable\n");

    /* Validate */
    size_t bad = validate(src, dst, size);
    if (bad == 0)
        printf("  PASS — data matches\n");
    else
        printf("  FAIL — %zu/%zu bytes wrong\n", bad, size);

    close(src_fd);
    close(dst_fd);
    munlock(src, size);
    munlock(dst, size);
    free(src);
    free(dst);

    return bad == 0;
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    /* AON DMA PLIC IRQ: hardware IRQ 289, /proc/interrupts shows it as
     * "RISC-V INTC" line offset 289 on the platform interrupt controller.
     * The actual line in /proc/interrupts uses the Linux IRQ number which
     * may differ — search by DMAC name if 289 lookup fails. */
    const int AON_DMA_IRQ = 289;

    /* Sizes to test: 4K, 64K, 1M, 16M */
    size_t default_sizes[] = {
        4  * 1024,
        64 * 1024,
        1  * 1024 * 1024,
        16 * 1024 * 1024,
    };

    int mdbfd = open("/dev/malloc_dmabuf", O_RDWR);
    if (mdbfd < 0)
        die("open /dev/malloc_dmabuf");

    int memcp_fd = open("/dev/es_memcp", O_RDWR);
    if (memcp_fd < 0)
        die("open /dev/es_memcp");

    /* Check if AON DMA IRQ appears in /proc/interrupts */
    long long irq_test = read_irq_count(AON_DMA_IRQ);
    if (irq_test < 0)
        printf("Note: IRQ %d not found in /proc/interrupts "
               "(may use different number)\n", AON_DMA_IRQ);

    int failures = 0;

    if (argc > 1) {
        /* Single size from command line */
        size_t sz = (size_t)strtoull(argv[1], NULL, 0);
        if (!run_test(mdbfd, memcp_fd, sz, AON_DMA_IRQ))
            failures++;
    } else {
        for (size_t i = 0; i < sizeof(default_sizes)/sizeof(default_sizes[0]); i++) {
            if (!run_test(mdbfd, memcp_fd, default_sizes[i], AON_DMA_IRQ))
                failures++;
        }
    }

    close(memcp_fd);
    close(mdbfd);

    printf("\n%s: %d test(s) failed\n",
           failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures ? 1 : 0;
}
