// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal ESWIN NPU EDMA copy test.
 *
 * Prepares a 1-op (EDMA) + 1-op (EVENT_SINK) network, loads it via
 * ES_NPU_IOCTL_MODEL_LOAD, submits one inference via ASYNC_SUBMIT_TASK +
 * GET_EVENT, and validates that the output equals the input.
 *
 * Build (on target or with riscv64 cross-compiler):
 *   gcc -O0 -g npu_test.c -o npu_test
 *
 * Run on the FML13V03 board:
 *   ./npu_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>

/* ------------------------------------------------------------------ */
/* Kernel integer type aliases                                          */
/* ------------------------------------------------------------------ */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* ------------------------------------------------------------------ */
/* DMA-BUF heap (standard Linux 5.6+ API)                             */
/* ------------------------------------------------------------------ */
#define DMA_HEAP_IOC_MAGIC 'H'
struct dma_heap_allocation_data {
    u64 len;
    u32 fd;
    u32 fd_flags;
    u64 heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC \
    _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

/* ------------------------------------------------------------------ */
/* es_vb_user.h types                                                  */
/* ------------------------------------------------------------------ */
typedef struct ES_DEV_BUF {
    u64 memFd;
    u64 offset;
    u64 size;
    u64 reserve;
} ES_DEV_BUF_S; /* 32 bytes */

/* ------------------------------------------------------------------ */
/* hetero_ioctl.h types                                                */
/* ------------------------------------------------------------------ */
enum {
    mem_flag_swap     = 0,
    mem_flag_input    = 1,
    mem_flag_output   = 2,
    mem_flag_remote   = 3,
    mem_flag_distribute = 4,
};

typedef struct _addrDesc {
    ES_DEV_BUF_S devBuf;     /* 32 bytes */
    int          flag;       /* mem_flag_* */
    int          bindId; 
    void        *virtAddr;
    uint32_t     memoryType; /* 0=DDR, 1=SRAM : 4 + 4 padding */
} addrDesc_t;  // 56 bytes confirmed.

typedef struct _addrListDesc {
    uint32_t     numAddress;
    addrDesc_t   addrDesc[0];
} addrListDesc_t;

#define DSP_MAX_CORE_NUM 4

typedef struct _modelShmDesc {
    uint16_t       kmdSubModelId;
    uint32_t       kmdNetworkAddrId;
    int32_t        dspFd[DSP_MAX_CORE_NUM];
    int32_t        batch_num;
    addrListDesc_t addrList;
} modelShmDesc_t;

struct win_ioctl_args {
    union {
        u64 shm_fd;
        struct {
            union { u32 frame_idx; u32 frame_idx_buff_size; };
            u16 tensor_size;
            u8  dump_enable;
            u64 dump_info;
        };
        u16 event_source_val;
    };
    u64  data;
    u64  pret;
    u32  stream_id;
    u16  model_idx;
    u32  version;
    u32  hetero_cmd;
};

enum NPU_HETERO_CMD {
    ASYNC_SUBMIT_TASK = 0,
    SYNC_EXECUTE_TASK,
};

#define ES_NPU_IOCTL_BASE 'n'
#define ES_NPU_IOWR(nr, type) _IOWR(ES_NPU_IOCTL_BASE, nr, type)
#define ES_NPU_IOR(nr, type)  _IOR(ES_NPU_IOCTL_BASE, nr, type)
#define ES_NPU_IOCTL_MODEL_LOAD        ES_NPU_IOWR(0x03, int)
#define ES_NPU_IOCTL_MODEL_UNLOAD      ES_NPU_IOWR(0x04, int)
#define ES_NPU_IOCTL_TASK_SUBMIT       ES_NPU_IOWR(0x05, int)
#define ES_NPU_IOCTL_GET_EVENT         ES_NPU_IOR(0x08, int)
#define ES_NPU_IOCTL_PREPARE_DMA_BUF   ES_NPU_IOWR(0x0f, int)
#define ES_NPU_IOCTL_UNPREPARE_DMA_BUF ES_NPU_IOWR(0x10, int)

union event_union {
    s16 event_sinks[4];
    u64 event_data;
};

/* ------------------------------------------------------------------ */
/* dla_interface.h types (packed + 64-byte aligned as in kernel)       */
/* ------------------------------------------------------------------ */
#define ALIGNMENT 64

/* DLA op type codes */
#define DLA_OP_EDMA        0
#define DLA_OP_EVENT_SINK  9

/* Memory types */
#define DLA_MEM_MC  0
#define DLA_MEM_CV  1
#define DLA_MEM_HW  2

/* Events */
#define DLA_EVENT_OP_COMPLETED 1

/* Interface version */
#define NPU_INTERFACE_MAJOR_VERSION    0x00
#define NPU_INTERFACE_MINOR_VERSION    0x00
#define NPU_INTERFACE_SUBMINOR_VERSION 0x03

/* E31 submodel type for event_op */
#define SUBMODEL_E31 2

#define HW_OP_NUM 0x20

struct npu_version {
    u8 major_version;
    u8 minor_version;
    u8 subminor_version;
    u8 reserved;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_network_desc {
    struct npu_version version;
    uint32_t  reserved;
    int16_t   operation_desc_index;
    int16_t   surface_desc_index;
    int16_t   dependency_graph_index;
    int16_t   lut_data_index;
    int16_t   op_config_index;
    uint16_t  num_operations;
    uint16_t  num_event_ops;
    uint16_t  num_luts;
    uint16_t  num_addresses;
    uint16_t  reserved0;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_consumer {
    int16_t index;
    uint8_t event;
    uint8_t res;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_common_op_desc {
    int16_t index;
    int8_t  roi_index;
    uint8_t op_type;
    uint8_t dependency_count;
    uint8_t reserved0[3];
    struct dla_consumer consumers[HW_OP_NUM];
    struct dla_consumer fused_parent;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_data_cube {
    uint16_t type;
    int16_t  address;
    uint32_t offset;
    uint32_t size;
    uint16_t batch;
    uint16_t width;
    uint16_t height;
    uint16_t channel;
    uint16_t reserved0;
    uint32_t line_stride;
    uint32_t surf_stride;
    uint32_t plane_stride;
} __attribute__((packed, aligned(ALIGNMENT)));

struct npu_edma_op_desc {
    uint32_t input_c0_bytes;
    uint32_t src_num_line;
    uint32_t src_stride_line_bytes;
    uint32_t src_num_surface;
    uint32_t src_stride_surface_bytes;
    uint32_t src_num_cube;
    uint32_t src_stride_cube_bytes;
    uint32_t src_num_colony;
    uint32_t output_c0_bytes;
    uint32_t dst_num_line;
    uint32_t dst_stride_line_bytes;
    uint32_t dst_num_surface;
    uint32_t dst_stride_surface_bytes;
    uint32_t dst_num_cube;
    uint32_t dst_stride_cube_bytes;
    uint32_t dst_num_colony;
} __attribute__((packed, aligned(ALIGNMENT)));

struct npu_edma_surface_desc {
    struct dla_data_cube src_data;
    struct dla_data_cube dst_data;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_event_op_desc {
    int16_t index;
    int8_t  submodel_type;
    int8_t  p2p_src;
    int8_t  p2p_dst;
} __attribute__((packed, aligned(ALIGNMENT)));

/*
 * We need the union sizes to lay out op_desc[] and surface_desc[].
 * Determine them at compile time via sizeof().  Rather than replicating
 * every member type, we use two opaque byte arrays sized to the
 * kernel-header-computed maximum members, then cast when writing fields.
 *
 * Sizes measured via sizeof() (packed+aligned(64) semantics):
 *   union dla_operation_container dominated by npu_conv_op_desc = 1984 bytes
 *   union dla_surface_container   dominated by dsp_surface_desc = 1024 bytes
 */
#define OP_DESC_ELEM_SIZE   1984
#define SURF_DESC_ELEM_SIZE 1024

/* ------------------------------------------------------------------ */
/* Compile-time sanity: our struct sizes must not exceed element size  */
/* ------------------------------------------------------------------ */
_Static_assert(sizeof(struct npu_edma_op_desc)    <= OP_DESC_ELEM_SIZE, "edma_op too big");
_Static_assert(sizeof(struct dla_event_op_desc)   <= OP_DESC_ELEM_SIZE, "event_op too big");
_Static_assert(sizeof(struct npu_edma_surface_desc) <= SURF_DESC_ELEM_SIZE, "edma_surf too big");
_Static_assert(sizeof(struct dla_network_desc)    == 2*ALIGNMENT, "network_desc wrong size");
_Static_assert(sizeof(struct dla_common_op_desc)  == 34*ALIGNMENT, "op_dep wrong size");

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
#define NUM_OPS        2   /* EDMA op (idx 0) + EVENT_SINK op (idx 1) */
#define COPY_SIZE      64  /* bytes to copy */

/* addrList indices */
#define IDX_NETWORK_DESC  0
#define IDX_DEP_GRAPH     1
#define IDX_OP_DESC       2
#define IDX_SURFACE_DESC  3
#define IDX_INPUT         4
#define IDX_OUTPUT        5
#define NUM_ADDR          6

/* modelShmDesc_t has flexible addrList; allocate it all in one buffer */
#define MODEL_SHM_SIZE \
    (sizeof(modelShmDesc_t) + NUM_ADDR * sizeof(addrDesc_t))

static int alloc_dma_buf(int heap_fd, size_t size)
{
    struct dma_heap_allocation_data alloc = {
        .len        = size,
        .fd_flags   = O_RDWR | O_DSYNC | O_SYNC | O_CLOEXEC,
        .heap_flags = 0,
    };
    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
        perror("DMA_HEAP_IOCTL_ALLOC");
        return -1;
    }
    return (int)alloc.fd;
}

static void *mmap_dma_buf(int fd, size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED_VALIDATE, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap dma_buf");
        return NULL;
    }
    return p;
}

static int prepare_dma_buf(int npu_fd, int dmabuf_fd)
{
    /*
     * PREPARE_DMA_BUF reads ES_DEV_BUF_S from arg, but the generic
     * ioctl handler first copies sizeof(win_ioctl_args) bytes.
     * Use a union-padded buffer with ES_DEV_BUF_S at offset 0.
     */
    union {
        struct win_ioctl_args w;
        ES_DEV_BUF_S b;
    } buf;
    memset(&buf, 0, sizeof(buf));
    buf.b.memFd   = (u64)dmabuf_fd;
    buf.b.offset  = 0;
    buf.b.size    = 0;
    buf.b.reserve = 0;
    if (ioctl(npu_fd, ES_NPU_IOCTL_PREPARE_DMA_BUF, &buf) < 0) {
        perror("ES_NPU_IOCTL_PREPARE_DMA_BUF");
        return -1;
    }
    return 0;
}

static int unprepare_dma_buf(int npu_fd, int dmabuf_fd)
{
    /* UNPREPARE_DMA_BUF reads a raw uint32_t fd from arg */
    union {
        struct win_ioctl_args w;
        u32 fd;
    } buf;
    memset(&buf, 0, sizeof(buf));
    buf.fd = (u32)dmabuf_fd;
    if (ioctl(npu_fd, ES_NPU_IOCTL_UNPREPARE_DMA_BUF, &buf) < 0) {
        perror("ES_NPU_IOCTL_UNPREPARE_DMA_BUF");
        return -1;
    }
    return 0;
}

int main(void)
{
    int ret = 0;

    /* ----- runtime size verification ----- */
    printf("struct sizes: network_desc=%zu common_op_desc=%zu addrDesc=%zu\n",
           sizeof(struct dla_network_desc),
           sizeof(struct dla_common_op_desc),
           sizeof(addrDesc_t));
    printf("  OP_DESC_ELEM=%d SURF_DESC_ELEM=%d MODEL_SHM=%zu\n",
           OP_DESC_ELEM_SIZE, SURF_DESC_ELEM_SIZE, MODEL_SHM_SIZE);

    /* ----- open DMA heap ----- */
    int heap_fd = open("/dev/dma_heap/mmz_nid_0_part_0", O_RDWR | O_CLOEXEC);
    if (heap_fd < 0) { perror("open /dev/dma_heap/mmz_nid_0_part_0"); return 1; }

    /* ----- allocate DMA buffers ----- */
    int model_fd = alloc_dma_buf(heap_fd, MODEL_SHM_SIZE);
    int net_fd   = alloc_dma_buf(heap_fd, sizeof(struct dla_network_desc));
    int dep_fd   = alloc_dma_buf(heap_fd, NUM_OPS * sizeof(struct dla_common_op_desc));
    int opd_fd   = alloc_dma_buf(heap_fd, NUM_OPS * OP_DESC_ELEM_SIZE);
    int sfd      = alloc_dma_buf(heap_fd, NUM_OPS * SURF_DESC_ELEM_SIZE);
    int in_fd    = alloc_dma_buf(heap_fd, COPY_SIZE);
    int out_fd   = alloc_dma_buf(heap_fd, COPY_SIZE);

    if (model_fd<0 || net_fd<0 || dep_fd<0 || opd_fd<0 ||
        sfd<0 || in_fd<0 || out_fd<0) {
        fprintf(stderr, "alloc failed\n"); return 1;
    }

    /* ----- mmap for CPU access ----- */
    modelShmDesc_t *shm  = mmap_dma_buf(model_fd, MODEL_SHM_SIZE);
    struct dla_network_desc *net = mmap_dma_buf(net_fd,
                                    sizeof(struct dla_network_desc));
    struct dla_common_op_desc *dep = mmap_dma_buf(dep_fd,
                                    NUM_OPS * sizeof(struct dla_common_op_desc));
    uint8_t *opd = mmap_dma_buf(opd_fd, NUM_OPS * OP_DESC_ELEM_SIZE);
    uint8_t *sdc = mmap_dma_buf(sfd,    NUM_OPS * SURF_DESC_ELEM_SIZE);
    uint8_t *inp = mmap_dma_buf(in_fd,  COPY_SIZE);
    uint8_t *out = mmap_dma_buf(out_fd, COPY_SIZE);

    if (!shm||!net||!dep||!opd||!sdc||!inp||!out) {
        fprintf(stderr, "mmap failed\n"); return 1;
    }

    /* ----- fill input buffer with test pattern ----- */
    for (int i = 0; i < COPY_SIZE; i++)
        inp[i] = (uint8_t)(i * 3 + 7);
    memset(out, 0, COPY_SIZE);

    /* ----- fill dla_network_desc ----- */
    memset(net, 0, sizeof(*net));
    net->version.major_version    = NPU_INTERFACE_MAJOR_VERSION;
    net->version.minor_version    = NPU_INTERFACE_MINOR_VERSION;
    net->version.subminor_version = NPU_INTERFACE_SUBMINOR_VERSION;
    net->operation_desc_index     = IDX_OP_DESC;
    net->surface_desc_index       = IDX_SURFACE_DESC;
    net->dependency_graph_index   = IDX_DEP_GRAPH;
    net->lut_data_index           = -1;
    net->op_config_index          = -1;
    net->num_operations           = NUM_OPS;
    net->num_event_ops            = 1;
    net->num_luts                 = 0;
    net->num_addresses            = NUM_ADDR;

    /* ----- fill dependency graph ----- */
    memset(dep, 0, NUM_OPS * sizeof(struct dla_common_op_desc));

    /* Op 0: EDMA - no producers, one consumer (op 1 on completion) */
    dep[0].index            = 0;
    dep[0].op_type          = DLA_OP_EDMA;
    dep[0].dependency_count = 0;
    /* mark unused by default */
    for (int i = 0; i < HW_OP_NUM; i++) {
        dep[0].consumers[i].index = -1;
        dep[0].consumers[i].event = 1;
    }
    dep[0].consumers[DLA_OP_EVENT_SINK].index = 1;
    dep[0].consumers[DLA_OP_EVENT_SINK].event = DLA_EVENT_OP_COMPLETED;
    dep[0].fused_parent.index = -1;
    dep[0].fused_parent.event = 1;

    /* Op 1: EVENT_SINK - depends on op 0 */
    dep[1].index            = 1;
    dep[1].op_type          = DLA_OP_EVENT_SINK;
    dep[1].dependency_count = 1;
    /* no consumers */
    for (int i = 0; i < HW_OP_NUM; i++) {
        dep[1].consumers[i].index = -1;
        dep[1].consumers[i].event = 1;
    }
    dep[1].fused_parent.index = -1;
    dep[1].fused_parent.event = 1;

    /* ----- fill op descriptors ----- */
    memset(opd, 0, NUM_OPS * OP_DESC_ELEM_SIZE);

    /* Op 0: EDMA - flat copy of COPY_SIZE bytes */
    {
        struct npu_edma_op_desc *e = (struct npu_edma_op_desc *)(opd + 0);
        e->input_c0_bytes          = COPY_SIZE;
        e->src_num_line            = 1;
        e->src_stride_line_bytes   = COPY_SIZE;
        e->src_num_surface         = 1;
        e->src_stride_surface_bytes = COPY_SIZE;
        e->src_num_cube            = 1;
        e->src_stride_cube_bytes   = 0;
        e->src_num_colony          = 1;
        e->output_c0_bytes         = COPY_SIZE;
        e->dst_num_line            = 1;
        e->dst_stride_line_bytes   = COPY_SIZE;
        e->dst_num_surface         = 1;
        e->dst_stride_surface_bytes = COPY_SIZE;
        e->dst_num_cube            = 1;
        e->dst_stride_cube_bytes   = 0;
        e->dst_num_colony          = 1;
    }

    /* Op 1: EVENT_SINK with event index 0, handled by E31 */
    {
        struct dla_event_op_desc *ev =
            (struct dla_event_op_desc *)(opd + OP_DESC_ELEM_SIZE);
        ev->index         = 0;
        ev->submodel_type = SUBMODEL_E31;
        ev->p2p_src       = 0;
        ev->p2p_dst       = 0;
    }

    /* ----- fill surface descriptors ----- */
    memset(sdc, 0, NUM_OPS * SURF_DESC_ELEM_SIZE);

    /* Surface 0: EDMA src=input buffer (IDX_INPUT), dst=output buffer (IDX_OUTPUT) */
    {
        struct npu_edma_surface_desc *es =
            (struct npu_edma_surface_desc *)(sdc + 0);
        es->src_data.type    = DLA_MEM_MC;
        es->src_data.address = IDX_INPUT;
        es->src_data.offset  = 0;
        es->src_data.size    = COPY_SIZE;
        es->dst_data.type    = DLA_MEM_MC;
        es->dst_data.address = IDX_OUTPUT;
        es->dst_data.offset  = 0;
        es->dst_data.size    = COPY_SIZE;
    }
    /* Surface 1: EVENT_SINK - leave zeroed */

    /* ----- fill model SHM ----- */
    memset(shm, 0, MODEL_SHM_SIZE);
    shm->kmdSubModelId   = 0;
    shm->kmdNetworkAddrId = IDX_NETWORK_DESC;
    shm->batch_num        = 1;
    for (int i = 0; i < DSP_MAX_CORE_NUM; i++)
        shm->dspFd[i] = -1;

    addrDesc_t *addr = shm->addrList.addrDesc;
    shm->addrList.numAddress = NUM_ADDR;

    /* Entry 0: network_desc buffer (static/swap) */
    addr[IDX_NETWORK_DESC].devBuf.memFd  = net_fd;
    addr[IDX_NETWORK_DESC].devBuf.offset = 0;
    addr[IDX_NETWORK_DESC].devBuf.size   = sizeof(struct dla_network_desc);
    addr[IDX_NETWORK_DESC].flag          = -1;
    addr[IDX_NETWORK_DESC].bindId        = -1;
    addr[IDX_NETWORK_DESC].virtAddr      = net;
    addr[IDX_NETWORK_DESC].memoryType    = 0;

    /* Entry 1: dependency graph buffer */
    addr[IDX_DEP_GRAPH].devBuf.memFd  = dep_fd;
    addr[IDX_DEP_GRAPH].devBuf.offset = 0;
    addr[IDX_DEP_GRAPH].devBuf.size   = NUM_OPS * sizeof(struct dla_common_op_desc);
    addr[IDX_DEP_GRAPH].flag          = -1;
    addr[IDX_DEP_GRAPH].bindId        = -1;
    addr[IDX_DEP_GRAPH].virtAddr      = dep;
    addr[IDX_DEP_GRAPH].memoryType    = 0;

    /* Entry 2: op descriptor buffer */
    addr[IDX_OP_DESC].devBuf.memFd  = opd_fd;
    addr[IDX_OP_DESC].devBuf.offset = 0;
    addr[IDX_OP_DESC].devBuf.size   = NUM_OPS * OP_DESC_ELEM_SIZE;
    addr[IDX_OP_DESC].flag          = -1;
    addr[IDX_OP_DESC].bindId        = -1;
    addr[IDX_OP_DESC].virtAddr      = opd;
    addr[IDX_OP_DESC].memoryType    = 0;

    /* Entry 3: surface descriptor buffer */
    addr[IDX_SURFACE_DESC].devBuf.memFd  = sfd;
    addr[IDX_SURFACE_DESC].devBuf.offset = 0;
    addr[IDX_SURFACE_DESC].devBuf.size   = NUM_OPS * SURF_DESC_ELEM_SIZE;
    addr[IDX_SURFACE_DESC].flag          = -1;
    addr[IDX_SURFACE_DESC].bindId        = -1;
    addr[IDX_SURFACE_DESC].virtAddr      = sdc;
    addr[IDX_SURFACE_DESC].memoryType    = 0;

    /*
     * Entries 4, 5: IO tensor placeholders.
     * devBuf.memFd = 0 → skipped by dla_import_dmabuf_from_model
     * (it skips fd <= 0). The actual per-frame DMA buffers are
     * supplied via the io_tensor_list at inference time.
     */
    addr[IDX_INPUT].devBuf.memFd  = -1; /* skip at model-load time */
    addr[IDX_INPUT].devBuf.size   = COPY_SIZE;
    addr[IDX_INPUT].flag          = mem_flag_input;
    addr[IDX_INPUT].bindId        = 0;  /* first input slot */
    addr[IDX_INPUT].memoryType    = 0;

    addr[IDX_OUTPUT].devBuf.memFd = -1; /* skip at model-load time */
    addr[IDX_OUTPUT].devBuf.size  = COPY_SIZE;
    addr[IDX_OUTPUT].flag         = mem_flag_output;
    addr[IDX_OUTPUT].bindId       = 0;  /* first output slot */
    addr[IDX_OUTPUT].memoryType   = 0;

    /* Make sure all writes are visible before the kernel reads the buffer */
    msync(shm, MODEL_SHM_SIZE, MS_SYNC);
    msync(net, sizeof(*net), MS_SYNC);
    msync(dep, NUM_OPS * sizeof(*dep), MS_SYNC);
    msync(opd, NUM_OPS * OP_DESC_ELEM_SIZE, MS_SYNC);
    msync(sdc, NUM_OPS * SURF_DESC_ELEM_SIZE, MS_SYNC);
    msync(inp, COPY_SIZE, MS_SYNC);
    msync(out, COPY_SIZE, MS_SYNC);

    /* ----- open NPU ----- */
    int npu_fd = open("/dev/npu0", O_RDWR);
    if (npu_fd < 0) { perror("open /dev/npu0"); return 1; }

    /* ----- prepare IO DMA buffers with the NPU ----- */
    if (prepare_dma_buf(npu_fd, in_fd)  < 0) goto err_close;
    if (prepare_dma_buf(npu_fd, out_fd) < 0) goto err_unprep_in;

    /* ----- load model ----- */
    int64_t model_id = -1;
    {
        struct win_ioctl_args arg = {};
        arg.shm_fd    = (u64)model_fd;
        arg.pret      = (u64)&model_id;
        if (ioctl(npu_fd, ES_NPU_IOCTL_MODEL_LOAD, &arg) < 0) {
            perror("ES_NPU_IOCTL_MODEL_LOAD");
            ret = 1; goto err_unprep_out;
        }
    }
    printf("Model loaded, model_id=%ld\n", (long)model_id);

    /* ----- prepare per-frame IO tensor list ----- */
    /*
     * The kernel reads (input_num + output_num) * sizeof(addrDesc_t) bytes
     * from win_arg.data.  We have 1 input + 1 output = 2 entries.
     * win_arg.tensor_size must equal io_mem_handle_size = 2*sizeof(addrDesc_t).
     */
    addrDesc_t io_tensors[2];
    memset(io_tensors, 0, sizeof(io_tensors));

    io_tensors[0].devBuf.memFd  = (u64)in_fd;
    io_tensors[0].devBuf.offset = 0;
    io_tensors[0].devBuf.size   = COPY_SIZE;
    io_tensors[0].flag          = mem_flag_input;
    io_tensors[0].bindId        = 0;
    io_tensors[0].memoryType    = 0;

    io_tensors[1].devBuf.memFd  = (u64)out_fd;
    io_tensors[1].devBuf.offset = 0;
    io_tensors[1].devBuf.size   = COPY_SIZE;
    io_tensors[1].flag          = mem_flag_output;
    io_tensors[1].bindId        = 0;
    io_tensors[1].memoryType    = 0;

    int64_t hw_error = -1;

    /* ----- submit async task ----- */
    {
        struct win_ioctl_args arg = {};
        arg.model_idx   = (u16)model_id;
        arg.tensor_size = (u16)(2 * sizeof(addrDesc_t));
        arg.data        = (u64)io_tensors; /* in: IO tensor list */
        arg.pret        = (u64)&hw_error;
        arg.hetero_cmd  = SYNC_EXECUTE_TASK;
        arg.frame_idx   = 0;
        arg.dump_enable = 0;

        printf("Submitting ASYNC_SUBMIT_TASK (tensor_size=%u)...\n",
               arg.tensor_size);
        if (ioctl(npu_fd, ES_NPU_IOCTL_TASK_SUBMIT, &arg) < 0) {
            perror("ES_NPU_IOCTL_TASK_SUBMIT");
            ret = 1; goto err_unload;
        }
    }
#if 0
    /* ----- wait for completion via GET_EVENT ----- */
    {
        union event_union event_buf = {};
        struct win_ioctl_args arg = {};
        arg.data  = (u64)&event_buf; /* out: event data */
        arg.pret  = 0;               /* no secondary event buffer */

        /* GET_EVENT is non-blocking; poll() blocks until NPU task completes */
        {
            struct pollfd pfd = { .fd = npu_fd, .events = POLLIN };
            int pret = poll(&pfd, 1, 5000 /* 5s timeout */);
            if (pret < 0) { perror("poll npu_fd"); ret = 1; goto err_unload; }
            if (pret == 0) { fprintf(stderr, "poll: NPU task timeout\n"); ret = 1; goto err_unload; }
        }
        printf("Waiting for ES_NPU_IOCTL_GET_EVENT...\n");
        if (ioctl(npu_fd, ES_NPU_IOCTL_GET_EVENT, &arg) < 0) {
            perror("ES_NPU_IOCTL_GET_EVENT");
            ret = 1; goto err_unload;
        }
        printf("GET_EVENT done: event_sinks=[%d, %d, %d, %d]\n",
               event_buf.event_sinks[0], event_buf.event_sinks[1],
               event_buf.event_sinks[2], event_buf.event_sinks[3]);
    }
#endif
    /* ----- validate result ----- */
    msync(out, COPY_SIZE, MS_INVALIDATE);
    int pass = 1;
    for (int i = 0; i < COPY_SIZE; i++) {
        uint8_t expected = (uint8_t)(i * 3 + 7);
        if (out[i] != expected) {
            fprintf(stderr, "MISMATCH at byte %d: expected 0x%02x got 0x%02x\n",
                    i, expected, out[i]);
            pass = 0;
        }
    }
    if (pass)
        printf("PASS: output matches input (EDMA copy of %d bytes)\n", COPY_SIZE);
    else
        printf("FAIL: output does not match\n");

    ret = pass ? 0 : 1;

err_unload:
    {
        struct win_ioctl_args arg = {};
        arg.model_idx = (u16)model_id;
        ioctl(npu_fd, ES_NPU_IOCTL_MODEL_UNLOAD, &arg);
    }
err_unprep_out:
    unprepare_dma_buf(npu_fd, out_fd);
err_unprep_in:
    unprepare_dma_buf(npu_fd, in_fd);
err_close:
    close(npu_fd);
    close(heap_fd);
    /* DMA buf fds are closed automatically via O_CLOEXEC but let's be explicit */
    close(model_fd); close(net_fd); close(dep_fd);
    close(opd_fd);   close(sfd);    close(in_fd); close(out_fd);
    return ret;
}
