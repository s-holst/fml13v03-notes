// SPDX-License-Identifier: GPL-2.0
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * 
 * Purpose
 * -------
 * 
 * Traces the driver interactions from npu programs such as eswin's sample_npu
 * and pretty-prints the passed structures. This file is self-contained in that
 * all eswin-specific interface definitions have been reproduced here.
 *
 * Interface definitions were taken from:
 * https://github.com/DC-DeepComputing/fml13v03_linux/tree/fml13v03-6.6.92
 * 
 * Paths of the original headers are in the comments.
 *  
 * Usage
 * -----
 * 
 * gcc -shared -fPIC -o npu_trace.so npu_trace.c -ldl
 * LD_PRELOAD=./npu_trace.so /opt/eswin/sample-code/npu_sample/npu_runtime_sample/bin/sample_npu -s 1
 * 
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>


#define OP_PRINT_LIMIT 16

#define tid_printf(fmt, ...) printf("[T%d] " fmt, (int)gettid(), ##__VA_ARGS__)
#define FD_STR(r) ((r) >= 0 ? "fd_" : ""), (r)
#define MEM_FLAG(f) ((f)==-1?"internal":(f)==1?"input":(f)==2?"output":"?")
#define call_ioctl(fd, req, arg) (fflush(stdout), original_ioctl(fd, req, arg))


/* ------------------------------------------------------------------ */
/* Kernel integer type aliases                                        */
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
/* ./include/uapi/linux/es_vb_user.h                                  */
/* ------------------------------------------------------------------ */

typedef struct ES_DEV_BUF {
    u64 memFd;
    u64 offset;
    u64 size;
    u64 reserve;
} ES_DEV_BUF_S; /* 32 bytes */


/* ------------------------------------------------------------------ */
/* ./drivers/soc/eswin/ai_driver/common/hetero_ioctl.h                */
/* ------------------------------------------------------------------ */

typedef struct _addrDesc {
    ES_DEV_BUF_S devBuf;     /* 32 bytes */
    int          flag;       /* mem_flag_* 4 */
    int          bindId;     // 4 
    void        *virtAddr;   // 8
    uint32_t     memoryType; /* 0=DDR, 1=SRAM 4 + 4 padding */
} addrDesc_t;  /* 56 bytes */

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

typedef struct _sram_info {
    s32 fd;
    u32 size;
} sram_info_t;

union event_union {
    s16 event_sinks[4];
    u64 event_data;
};

/* ------------------------------------------------------------------ */
/* ./include/uapi/linux/dma-buf.h                                     */
/* ------------------------------------------------------------------ */

struct dma_buf_sync {
    u64 flags;
};

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)
#define DMA_BUF_SYNC_VALID_FLAGS_MASK \
    (DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END)

#define DMA_BUF_NAME_LEN    32

#define DMA_BUF_BASE        'b'
#define DMA_BUF_IOCTL_SYNC  _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)
#define DMA_BUF_SET_NAME    _IOW(DMA_BUF_BASE, 1, const char *)
#define DMA_BUF_SET_NAME_A  _IOW(DMA_BUF_BASE, 1, u32)
#define DMA_BUF_SET_NAME_B  _IOW(DMA_BUF_BASE, 1, u64)

/* ------------------------------------------------------------------ */
/* ./include/uapi/linux/dma-heap.h                                    */
/* ------------------------------------------------------------------ */

/* Valid FD_FLAGS are O_CLOEXEC, O_RDONLY, O_WRONLY, O_RDWR */
#define DMA_HEAP_VALID_FD_FLAGS (O_CLOEXEC | O_ACCMODE)

/* Currently no heap flags */
#define DMA_HEAP_VALID_HEAP_FLAGS (0)

/**
 * struct dma_heap_allocation_data - metadata passed from userspace for
 *                                      allocations
 * @len:        size of the allocation
 * @fd:         will be populated with a fd which provides the
 *              handle to the allocated dma-buf
 * @fd_flags:   file descriptor flags used when allocating
 * @heap_flags: flags passed to heap
 *
 * Provided by userspace as an argument to the ioctl
 */
struct dma_heap_allocation_data {
    u64 len;
    u32 fd;
    u32 fd_flags;
    u64 heap_flags;
};

#define DMA_HEAP_IOC_MAGIC 'H'

/**
 * DMA_HEAP_IOCTL_ALLOC - allocate memory from pool
 *
 * Takes a dma_heap_allocation_data struct and returns it with the fd field
 * populated with the dmabuf handle of the allocation.
 */
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, \
                                   struct dma_heap_allocation_data)

/* ----------------------------------------------------------------- */
/* es_rsvmem_heap/include/uapi/linux/eswin_rsvmem_common.h           */
/* Vendor extensions for /dev/dma_heap/mmz_nid_* devices             */
/* (ESWIN_HEAP_IOC_MAGIC == DMA_HEAP_IOC_MAGIC == 'H', cmd 0x0:      */
/*  ESWIN_HEAP_IOCTL_ALLOC == DMA_HEAP_IOCTL_ALLOC — same number)    */
/* ----------------------------------------------------------------- */

/* Valid FD_FLAGS are O_CLOEXEC, O_RDONLY, O_WRONLY, O_RDWR, O_SYNC */
#define ESWIN_HEAP_VALID_FD_FLAGS (O_CLOEXEC | O_ACCMODE | O_SYNC)

/* Add HEAP_SPRAM_FORCE_CONTIGUOUS heap flags for ESWIN SPRAM HEAP */
#define HEAP_FLAGS_SPRAM_FORCE_CONTIGUOUS (1 << 0)
#define ESWIN_HEAP_VALID_HEAP_FLAGS (HEAP_FLAGS_SPRAM_FORCE_CONTIGUOUS)

/* ------------------------------------------------------------------ */
/* ./drivers/soc/eswin/ai_driver/common/hetero_ioctl.h                */
/* npu ioctl commands (/dev/npu*)                                     */
/* ------------------------------------------------------------------ */

#define ES_NPU_IOCTL_BASE 'n'
#define ES_NPU_IO(nr) _IO(ES_NPU_IOCTL_BASE, nr)
#define ES_NPU_IOR(nr, type) _IOR(ES_NPU_IOCTL_BASE, nr, type)
#define ES_NPU_IOW(nr, type) _IOW(ES_NPU_IOCTL_BASE, nr, type)
#define ES_NPU_IOWR(nr, type) _IOWR(ES_NPU_IOCTL_BASE, nr, type)

#define ES_NPU_IOCTL_GET_VERSION ES_NPU_IOR(0X00, int)
#define ES_NPU_IOCTL_GET_PROPERTY ES_NPU_IOR(0X01, int)
#define ES_NPU_IOCTL_GET_NUM_DEV ES_NPU_IOR(0X02, int)

#define ES_NPU_IOCTL_MODEL_LOAD ES_NPU_IOWR(0X03, int)
#define ES_NPU_IOCTL_MODEL_UNLOAD ES_NPU_IOWR(0X04, int)

#define ES_NPU_IOCTL_TASK_SUBMIT ES_NPU_IOWR(0X05, int)
#define ES_NPU_IOCTL_TASKS_GET_RESULT ES_NPU_IOWR(0X06, int)
#define ES_NPU_IOCTL_HETERO_CMD ES_NPU_IOWR(0x7, int)

#define ES_NPU_IOCTL_GET_EVENT ES_NPU_IOR(0X08, int)
#define ES_NPU_IOCTL_SET_EVENT ES_NPU_IOWR(0X09, int)

#define ES_NPU_IOCTL_GET_SRAM_FD ES_NPU_IOR(0X0a, int)
#define ES_NPU_IOCTL_HANDLE_PERF ES_NPU_IOR(0X0b, int)
#define ES_NPU_IOCTL_GET_PERF_DATA ES_NPU_IOR(0X0c, int)

#define ES_NPU_IOCTL_MUTEX_LOCK ES_NPU_IOR(0X0d, int)
#define ES_NPU_IOCTL_MUTEX_UNLOCK ES_NPU_IOWR(0X0e, int)
#define ES_NPU_IOCTL_PREPARE_DMA_BUF ES_NPU_IOWR(0xf, int)
#define ES_NPU_IOCTL_UNPREPARE_DMA_BUF ES_NPU_IOWR(0x10, int)

#define ES_NPU_IOCTL_MUTEX_TRYLOCK ES_NPU_IOWR(0x11, int)


/* ----------------------------------------------------------------- */
/* ./drivers/staging/media/eswin/vdec/hantrovcmd.h                   */
/* ./drivers/staging/media/eswin/vdec/hantrodec.h                    */
/* ----------------------------------------------------------------- */

/* exchange_parameter — HANTRO_VCMD_IOCH_RESERVE_CMDBUF argument */
struct exchange_parameter {
	u64 executing_time;  /* input: estimated executing time in hw */
	u16 module_type;     /* input: type of HW (0=vc8000e,1=cutree,2=vc8000d,3=jpege,4=jpegd) */
	u16 cmdbuf_size;     /* input: the size of cmdbuf requested */
	u16 priority;        /* input: priority (0=normal/background, 1=high/live) */
	u16 cmdbuf_id;       /* output: unique id of the cmdbuf in driver */
	u16 core_id;         /* used for polling */
	u16 nid;             /* which die will be used, 0xFFFF: not specify */
};

/* dmabuf_cfg — HANTRODEC_IOC_DMA_HEAP_GET_IOVA argument */
struct dmabuf_cfg {
	int dmabuf_fd;       /* dma buf file descriptor */
	unsigned long iova;  /* I/O virtual address */
};

/* regsize_desc — HANTRODEC_IOCGHWIOSIZE argument */
struct regsize_desc {
	u32 slice; /* id of the slice */
	u32 id;    /* id of the subsystem */
	u32 type;  /* type of core to be written */
	u32 size;  /* iosize of the core */
};

/* subsys_desc — HANTRODEC_IOX_SUBSYS argument */
struct subsys_desc {
	u32 subsys_num;      /* total subsystems count */
	u32 subsys_vcmd_num; /* subsystems with vcmd */
};

/* cmdbuf_mem_parameter — HANTRO_VCMD_IOCH_GET_CMDBUF_PARAMETER argument */
/* (addr_t == size_t == u64 on 64-bit RISC-V) */
struct cmdbuf_mem_parameter {
	u32 *virt_cmdbuf_addr;
	u64 phy_cmdbuf_addr;         /* cmdbuf pool base physical address */
	u64 mmu_phy_cmdbuf_addr;     /* cmdbuf pool base mmu mapping address */
	u32 cmdbuf_total_size;       /* cmdbuf pool total size in bytes */
	u16 cmdbuf_unit_size;        /* one cmdbuf size in bytes */
	u32 *virt_status_cmdbuf_addr;
	u64 phy_status_cmdbuf_addr;  /* status cmdbuf pool base physical address */
	u64 mmu_phy_status_cmdbuf_addr; /* status cmdbuf pool base mmu mapping address */
	u32 status_cmdbuf_total_size;/* status cmdbuf pool total size in bytes */
	u16 status_cmdbuf_unit_size; /* one status cmdbuf size in bytes */
	u64 base_ddr_addr;           /* pcie base ddr addr (0 for non-pcie) */
};

/* config_parameter — HANTRO_VCMD_IOCH_GET_VCMD_PARAMETER argument */
struct config_parameter {
	u16 module_type;             /* input: vc8000e=0,cutree=1,vc8000d=2,jpege=3,jpegd=4 */
	u16 vcmd_core_num;           /* output: number of vcmd cores with this module_type */
	/* output: submodule addresses; 0xffff = does not exist */
	u16 submodule_main_addr;
	u16 submodule_dec400_addr;
	u16 submodule_L2Cache_addr;
	u16 submodule_MMU_addr;
	u16 submodule_MMUWrite_addr;
	u16 submodule_axife_addr;
	u16 config_status_cmdbuf_id; /* output: status cmdbuf id for register analysis */
	u32 vcmd_hw_version_id;      /* output: vcmd hardware version id */
};

#define HANTRO_VCMD_IOC_MAGIC 'v'
#define HANTRODEC_IOC_MAGIC   'k'

#define HANTRODEC_IOCGHWIOSIZE            _IOR( HANTRODEC_IOC_MAGIC,   4,  struct regsize_desc*)
#define HANTRODEC_IOX_SUBSYS              _IOWR(HANTRODEC_IOC_MAGIC,   25, struct subsys_desc*)
#define HANTRO_VCMD_IOCH_GET_CMDBUF_PARAMETER _IOWR(HANTRO_VCMD_IOC_MAGIC, 20, struct cmdbuf_mem_parameter*)
#define HANTRO_VCMD_IOCH_GET_VCMD_PARAMETER   _IOWR(HANTRO_VCMD_IOC_MAGIC, 24, struct config_parameter*)
#define HANTRO_VCMD_IOCH_RESERVE_CMDBUF  _IOWR(HANTRO_VCMD_IOC_MAGIC, 25, struct exchange_parameter*)
#define HANTRO_VCMD_IOCH_LINK_RUN_CMDBUF _IOR( HANTRO_VCMD_IOC_MAGIC, 26, u16*)
#define HANTRO_VCMD_IOCH_WAIT_CMDBUF     _IOR( HANTRO_VCMD_IOC_MAGIC, 27, u16*)
#define HANTRO_VCMD_IOCH_RELEASE_CMDBUF  _IOR( HANTRO_VCMD_IOC_MAGIC, 28, u16*)
#define HANTRODEC_IOC_DMA_HEAP_GET_IOVA  _IOR( HANTRODEC_IOC_MAGIC,   33, struct dmabuf_cfg*)
#define HANTRODEC_IOC_DMA_HEAP_PUT_IOVA  _IOR( HANTRODEC_IOC_MAGIC,   34, unsigned int *)

/* ----------------------------------------------------------------- */
/* ./drivers/soc/eswin/ai_driver/dsp/dsp_ioctl_if.h                  */
/* ----------------------------------------------------------------- */

#define ES_DSP_IOCTL_MAGIC 'e'
#define DSP_IOCTL_ALLOC          _IO(ES_DSP_IOCTL_MAGIC,  1)
#define DSP_IOCTL_FREE           _IO(ES_DSP_IOCTL_MAGIC,  2)
#define DSP_IOCTL_QUEUE          _IO(ES_DSP_IOCTL_MAGIC,  3)
#define DSP_IOCTL_QUEUE_NS       _IO(ES_DSP_IOCTL_MAGIC,  4)
#define DSP_IOCTL_IMPORT         _IO(ES_DSP_IOCTL_MAGIC,  5)
#define DSP_IOCTL_ALLOC_COHERENT _IO(ES_DSP_IOCTL_MAGIC,  6)
#define DSP_IOCTL_FREE_COHERENT  _IO(ES_DSP_IOCTL_MAGIC,  7)
#define DSP_IOCTL_REG_TASK       _IO(ES_DSP_IOCTL_MAGIC,  8)
#define DSP_IOCTL_TEST_INFO      _IO(ES_DSP_IOCTL_MAGIC,  9)
#define DSP_IOCTL_DMA_TEST       _IO(ES_DSP_IOCTL_MAGIC, 10)
#define DSP_IOCTL_LOAD_OP        _IO(ES_DSP_IOCTL_MAGIC, 11)
#define DSP_IOCTL_UNLOAD_OP          _IO(ES_DSP_IOCTL_MAGIC, 12)
#define DSP_IOCTL_SUBMIT_TSK         _IO(ES_DSP_IOCTL_MAGIC, 13)
#define DSP_IOCTL_WAIT_IRQ           _IO(ES_DSP_IOCTL_MAGIC, 14)
#define DSP_IOCTL_SEND_ACK           _IO(ES_DSP_IOCTL_MAGIC, 15)
#define DSP_IOCTL_QUERY_TASK         _IO(ES_DSP_IOCTL_MAGIC, 16)
#define DSP_IOCTL_SUBMIT_TSK_ASYNC   _IO(ES_DSP_IOCTL_MAGIC, 17)
#define DSP_IOCTL_GET_CMA_INFO       _IO(ES_DSP_IOCTL_MAGIC, 18)
#define DSP_IOCTL_PROCESS_REPORT     _IO(ES_DSP_IOCTL_MAGIC, 19)
#define DSP_IOCTL_PREPARE_DMA        _IO(ES_DSP_IOCTL_MAGIC, 20)
#define DSP_IOCTL_UNPREPARE_DMA      _IO(ES_DSP_IOCTL_MAGIC, 21)
#define DSP_IOCTL_ENABLE_PERF        _IO(ES_DSP_IOCTL_MAGIC, 22)
#define DSP_IOCTL_GET_PERF_DATA      _IO(ES_DSP_IOCTL_MAGIC, 23)
#define DSP_IOCTL_GET_FW_PERF_DATA   _IO(ES_DSP_IOCTL_MAGIC, 24)
#define DSP_IOCTL_SUBMIT_TSKS_ASYNC  _IO(ES_DSP_IOCTL_MAGIC, 25)
#define DSP_IOCTL_GET_CUR_OP_PERF_DATA _IO(ES_DSP_IOCTL_MAGIC, 26)

/* ------------------------------------------------------------------ */
/* ./include/uapi/linux/es_vb_user.h                                  */
/* ./include/uapi/linux/mmz_vb.h                                      */
/* ------------------------------------------------------------------ */

#define ES_VB_MAX_MOD_POOL   16
#define ES_MAX_MMZ_NAME_LEN  64

typedef u32 VB_POOL;

typedef enum {
    SYS_CACHE_MODE_NOCACHE = 0,
    SYS_CACHE_MODE_CACHED  = 1,
} SYS_CACHE_MODE_E;

typedef enum esVB_UID_E {
    VB_UID_PRIVATE = 0, VB_UID_COMMON, VB_UID_VI, VB_UID_VO, VB_UID_VPS,
    VB_UID_VENC, VB_UID_VDEC, VB_UID_HAE, VB_UID_USER, VB_UID_BUTT, VB_UID_MAX,
} esVB_UID_E;

typedef enum {
    EIC770X_LOGICAL_FLAT_MEM_NODE_0     = 0,
    EIC770X_LOGICAL_FLAT_MEM_NODE_1     = 1,
    EIC770X_LOGICAL_SPRAM_NODE_0        = 500,
    EIC770X_LOGICAL_SPRAM_NODE_1        = 501,
    EIC770X_LOGICAL_INTERLEAVE_MEM_NODE = 1000,
} EIC770X_LOGICAL_MEM_NODE_E;

struct esVB_POOL_CONFIG_S {
    u64              blkSize;
    u32              blkCnt;
    SYS_CACHE_MODE_E enRemapMode;
    char             mmzName[ES_MAX_MMZ_NAME_LEN];
};

struct esVB_CONFIG_S {
    u32                      poolCnt;
    struct esVB_POOL_CONFIG_S poolCfgs[ES_VB_MAX_MOD_POOL];
};

struct esVB_SET_CFG_REQ_S  { esVB_UID_E uid; struct esVB_CONFIG_S cfg; };
struct esVB_SET_CFG_CMD_S  { struct esVB_SET_CFG_REQ_S CfgReq; };

struct esVB_GET_CFG_REQ_S  { esVB_UID_E uid; };
struct esVB_GET_CFG_RSP_S  { struct esVB_CONFIG_S cfg; };
struct esVB_GET_CFG_CMD_S  { struct esVB_GET_CFG_REQ_S req; struct esVB_GET_CFG_RSP_S rsp; };

struct esVB_INIT_CFG_REQ_S   { esVB_UID_E uid; };
struct esVB_INIT_CFG_CMD_S   { struct esVB_INIT_CFG_REQ_S req; };

struct esVB_UNINIT_CFG_REQ_S { esVB_UID_E uid; };
struct esVB_UNINIT_CFG_CMD_S { struct esVB_UNINIT_CFG_REQ_S req; };

struct esVB_CREATE_POOL_REQ_S  { struct esVB_POOL_CONFIG_S req; };
struct esVB_CREATE_POOL_RESP_S { u32 PoolId; };
struct esVB_CREATE_POOL_CMD_S  { struct esVB_CREATE_POOL_REQ_S PoolReq; struct esVB_CREATE_POOL_RESP_S PoolResp; };

struct esVB_DESTORY_POOL_REQ_S  { u32 PoolId; };
struct esVB_DESTORY_POOL_RESP_S { u32 Result; };
struct esVB_DESTORY_POOL_CMD_S  { struct esVB_DESTORY_POOL_REQ_S req; struct esVB_DESTORY_POOL_RESP_S rsp; };

struct esVB_GET_BLOCK_REQ_S  { esVB_UID_E uid; VB_POOL poolId; u64 blkSize; char mmzName[ES_MAX_MMZ_NAME_LEN]; };
struct esVB_GET_BLOCK_RESP_S { u64 actualBlkSize; int fd; int nr; };
struct esVB_GET_BLOCK_CMD_S  { struct esVB_GET_BLOCK_REQ_S getBlkReq; struct esVB_GET_BLOCK_RESP_S getBlkResp; };

struct esVB_RETRIEVE_MEM_NODE_CMD_S { int fd; void *cpu_vaddr; EIC770X_LOGICAL_MEM_NODE_E numa_node; };
struct esVB_DMABUF_SIZE_CMD_S       { int fd; u64 size; };
struct esVB_DMABUF_REFCOUNT_CMD_S   { int fd; u64 refCnt; };

#define MMZ_VB_IOC_MAGIC               'M'
#define MMZ_VB_IOCTL_GET_BLOCK         _IOWR(MMZ_VB_IOC_MAGIC, 0x0, struct esVB_GET_BLOCK_CMD_S)
#define MMZ_VB_IOCTL_SET_CFG           _IOWR(MMZ_VB_IOC_MAGIC, 0x1, struct esVB_SET_CFG_CMD_S)
#define MMZ_VB_IOCTL_GET_CFG           _IOWR(MMZ_VB_IOC_MAGIC, 0x2, struct esVB_GET_CFG_CMD_S)
#define MMZ_VB_IOCTL_INIT_CFG          _IOWR(MMZ_VB_IOC_MAGIC, 0x3, struct esVB_INIT_CFG_CMD_S)
#define MMZ_VB_IOCTL_UNINIT_CFG        _IOWR(MMZ_VB_IOC_MAGIC, 0x4, struct esVB_UNINIT_CFG_CMD_S)
#define MMZ_VB_IOCTL_CREATE_POOL       _IOWR(MMZ_VB_IOC_MAGIC, 0x5, struct esVB_CREATE_POOL_CMD_S)
#define MMZ_VB_IOCTL_DESTORY_POOL      _IOWR(MMZ_VB_IOC_MAGIC, 0x6, struct esVB_DESTORY_POOL_CMD_S)
#define MMZ_VB_IOCTL_DMABUF_REFCOUNT   _IOR( MMZ_VB_IOC_MAGIC, 0xc, struct esVB_DMABUF_REFCOUNT_CMD_S)
#define MMZ_VB_IOCTL_RETRIEVE_MEM_NODE _IOR( MMZ_VB_IOC_MAGIC, 0xd, struct esVB_RETRIEVE_MEM_NODE_CMD_S)
#define MMZ_VB_IOCTL_DMABUF_SIZE       _IOR( MMZ_VB_IOC_MAGIC, 0xe, struct esVB_DMABUF_SIZE_CMD_S)
#define MMZ_VB_IOCTL_FLUSH_ALL         _IOW( MMZ_VB_IOC_MAGIC, 0xf, int)

/* ------------------------------------------------------------------ */
/* ./include/uapi/linux/dma_memcp.h                                   */
/* ------------------------------------------------------------------ */

#define ESW_MEMCP_MAGIC 'M'

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

#define ESW_CMDQ_ADD_TASK _IOW(ESW_MEMCP_MAGIC, 1, struct esw_memcp_f2f_cmd)
#define ESW_CMDQ_SYNC     _IO( ESW_MEMCP_MAGIC, 2)
#define ESW_CMDQ_QUERY    _IOR(ESW_MEMCP_MAGIC, 3, struct esw_cmdq_query)

/* --------------------------------------------------------------------- */
/* ./drivers/staging/media/eswin/es-media-ext/include/es_media_ext_drv.h */
/* --------------------------------------------------------------------- */

typedef struct {
    unsigned short group;
    unsigned short channel;
} es_channel_t;

typedef enum {
    MODULE_UNKNOWN = 0,
    MODULE_PROC,
    MODULE_BIND,
    MODULE_UNBIND,
} MODULE_EVENT_E;

typedef struct {
    MODULE_EVENT_E id;
    unsigned int token;
    union {
        es_channel_t chn;
        unsigned int value;
    };
} es_module_event_t;

#define ES_IOC_MAGIC_M 'm'
#define ES_IOC_MAGIC_C 'c'
#define ES_IOC_BASE    0

#define ES_MOD_IOC_GET_EVENT            _IOR(ES_IOC_MAGIC_M, ES_IOC_BASE + 0, es_module_event_t *)
#define ES_MOD_IOC_PROC_SEND_MODULE     _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 1, void *)
#define ES_MOD_IOC_PROC_SEND_GRP_TITLE  _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 2, void *)
#define ES_MOD_IOC_PROC_SEND_GRP_DATA   _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 3, void *)
#define ES_MOD_IOC_PUB_USER             _IO( ES_IOC_MAGIC_M, ES_IOC_BASE + 4)
#define ES_MOD_IOC_PROC_SET_SECTION     _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 5, void *)
#define ES_MOD_IOC_PROC_SET_TIMEOUT     _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 6, unsigned int *)

#define ES_CHN_IOC_COUNT_ADD            _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 0, unsigned int *)
#define ES_CHN_IOC_COUNT_SUB            _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 1, unsigned int *)
#define ES_CHN_IOC_COUNT_GET            _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 2, unsigned int *)
#define ES_CHN_IOC_ASSIGN_CHANNEL       _IOW( ES_IOC_MAGIC_C, ES_IOC_BASE + 3, es_channel_t *)
#define ES_CHN_IOC_UNASSIGN_CHANNEL     _IOW( ES_IOC_MAGIC_C, ES_IOC_BASE + 4, es_channel_t *)
#define ES_CHN_IOC_WAKEUP_COUNT_SET     _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 5, unsigned int *)
#define ES_CHN_IOC_WAKEUP_COUNT_GET     _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 6, unsigned int *)

/* ------------------------------------------------------------------------------ */
/* ./drivers/staging/media/eswin/hae/hal/os/linux/kernel/gc_hal_kernel_driver.c   */
/* ./drivers/staging/media/eswin/hae/hal/os/linux/kernel/gc_hal_kernel_os.h       */
/* ./drivers/staging/media/eswin/hae/hal/kernel/inc/shared/gc_hal_driver_shared.h */
/* ./drivers/staging/media/eswin/hae/hal/kernel/inc/shared/gc_hal_enum_shared.h   */
/* ------------------------------------------------------------------------------ */

#define IOCTL_GCHAL_INTERFACE           30000
#define IOCTL_GCHAL_PROFILER_INTERFACE  30001
#define IOCTL_GCHAL_TERMINATE           30002
#define IOCTL_ESW_ALLOC_IOVA            4000
#define IOCTL_ESW_FREE_IOVA             4001

typedef struct {
    uint64_t InputBuffer;
    uint64_t InputBufferSize;
    uint64_t OutputBuffer;
    uint64_t OutputBufferSize;
} es_hae_driver_args_t;

/* First fields of gcsHAL_INTERFACE, enough to read command + metadata */
typedef struct {
    int      command;       /* gceHAL_COMMAND_CODES */
    int      hardwareType;
    uint32_t devIndex;
    uint32_t coreIndex;
    int      status;
    int      engine;
    /* large union follows, not decoded here */
} es_hae_iface_hdr_t;

typedef struct {
    int32_t  fd;
    uint64_t iova;
    int32_t  error;
} es_hae_dmabuf_cfg_t;

static const char *gcvHAL_cmd_str(int cmd) {
    switch (cmd) {
    case  0: return "CHIP_INFO";
    case  1: return "VERSION";
    case  2: return "QUERY_CHIP_IDENTITY";
    case  3: return "QUERY_CHIP_OPTION";
    case  4: return "QUERY_CHIP_FREQUENCY";
    case  5: return "QUERY_VIDEO_MEMORY";
    case  6: return "ALLOCATE_LINEAR_VIDEO_MEMORY";
    case  7: return "WRAP_USER_MEMORY";
    case  8: return "RELEASE_VIDEO_MEMORY";
    case  9: return "LOCK_VIDEO_MEMORY";
    case 10: return "UNLOCK_VIDEO_MEMORY";
    case 11: return "BOTTOM_HALF_UNLOCK_VIDEO_MEMORY";
    case 12: return "MAP_MEMORY";
    case 13: return "UNMAP_MEMORY";
    case 14: return "CACHE";
    case 15: return "ATTACH";
    case 16: return "DETACH";
    case 17: return "EVENT_COMMIT";
    case 18: return "COMMIT";
    case 19: return "SET_TIMEOUT";
    case 20: return "USER_SIGNAL";
    case 21: return "SIGNAL";
    case 22: return "SET_PROFILE_SETTING";
    case 23: return "READ_PROFILER_REGISTER_SETTING";
    case 24: return "READ_ALL_PROFILE_REGISTERS_PART1";
    case 25: return "READ_ALL_PROFILE_REGISTERS_PART2";
    case 26: return "DATABASE";
    case 27: return "CONFIG_POWER_MANAGEMENT";
    case 28: return "DEBUG_DUMP";
    case 29: return "READ_REGISTER";
    case 30: return "WRITE_REGISTER";
    case 31: return "PROFILE_REGISTERS_2D";
    case 32: return "GET_BASE_ADDRESS";
    case 33: return "GET_FRAME_INFO";
    case 34: return "SET_VIDEO_MEMORY_METADATA";
    case 35: return "QUERY_COMMAND_BUFFER";
    case 36: return "QUERY_RESET_TIME_STAMP";
    case 37: return "CREATE_NATIVE_FENCE";
    case 38: return "WAIT_NATIVE_FENCE";
    case 39: return "WAIT_FENCE";
    case 40: return "EXPORT_VIDEO_MEMORY";
    case 41: return "NAME_VIDEO_MEMORY";
    case 42: return "IMPORT_VIDEO_MEMORY";
    case 43: return "DEVICE_MUTEX";
    case 44: return "DEC200_TEST";
    case 45: return "DEC300_READ";
    case 46: return "DEC300_WRITE";
    case 47: return "DEC300_FLUSH";
    case 48: return "DEC300_FLUSH_WAIT";
    case 49: return "SHBUF";
    case 50: return "GET_GRAPHIC_BUFFER_FD";
    case 51: return "UPDATE_DEBUG_CALLBACK";
    case 52: return "CONFIG_CTX_FRAMEWORK";
    case 53: return "ALLOCATE_NON_PAGED_MEMORY";
    case 54: return "FREE_NON_PAGED_MEMORY";
    case 55: return "WRITE_DATA";
    case 56: return "APB_AXIFE_ACCESS";
    case 57: return "RESET";
    case 58: return "COMMIT_DONE";
    case 59: return "GET_VIDEO_MEMORY_FD";
    case 60: return "GET_PROFILE_SETTING";
    case 61: return "READ_REGISTER_EX";
    case 62: return "WRITE_REGISTER_EX";
    case 63: return "SET_POWER_MANAGEMENT_STATE";
    case 64: return "QUERY_POWER_MANAGEMENT_STATE";
    case 65: return "QUERY_CPU_FREQUENCY";
    case 66: return "DUMP_GPU_STATE";
    case 67: return "SYNC_VIDEO_MEMORY";
    case 68: return "CANCEL_JOB";
    case 69: return "TIMESTAMP";
    case 70: return "SET_FSCALE_VALUE";
    case 71: return "GET_FSCALE_VALUE";
    case 72: return "DESTROY_MMU";
    case 73: return "WRITE_REG_VALUE";
    default: return "?";
    }
}

/* ------------------------------------------------------------------ */
/* ./drivers/soc/eswin/ai_driver/common/dla_interface.h               */
/* ------------------------------------------------------------------ */

#define ALIGNMENT 64

/* Max number of DLA op types */
#define HW_OP_NUM 0x20

/* DLA op type codes, NN is formed from these ops. */
#define DLA_OP_EDMA 0
#define DLA_OP_CONV 1
#define DLA_OP_SDP 2
#define DLA_OP_PDP 3
#define DLA_OP_RUBIK 4
#define DLA_KMD_OP_DSP_0 5
#define DLA_KMD_OP_DSP_1 6
#define DLA_KMD_OP_DSP_2 7
#define DLA_KMD_OP_DSP_3 8
#define DLA_OP_EVENT_SINK  9
#define DLA_OP_EVENT_SOURCE 0xa
#define DLA_OP_DSP_0 0xb
#define DLA_OP_DSP_1 0xc
#define DLA_OP_DSP_2 0xd
#define DLA_OP_DSP_3 0xe
#define DLA_OP_HAE 0xf
#define DLA_OP_GPU 0x10
#define DLA_OP_SWITCH 0x11
#define DLA_OP_MERGE 0x12

/* Memory types */
#define DLA_MEM_MC  0
#define DLA_MEM_CV  1
#define DLA_MEM_HW  2

/* Events */
#define DLA_EVENT_OP_COMPLETED 1
#define DLA_EVENT_OP_PROGRAMMED 2
#define DLA_EVENT_OP_ENABLED 3
#define DLA_EVENT_CDMA_WT_DONE 4
#define DLA_EVENT_CDMA_DT_DONE 5

/* Interface version */
#define NPU_INTERFACE_MAJOR_VERSION    0x00
#define NPU_INTERFACE_MINOR_VERSION    0x00
#define NPU_INTERFACE_SUBMINOR_VERSION 0x03


/* E31 submodel type for event_op */
#define SUBMODEL_E31 2


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
    int16_t   op_config_index;  /* for DLA_KMD_OP_DSP_{0,1,2,3} */
    uint16_t  num_operations;
    uint16_t  num_event_ops;
    uint16_t  num_luts;
    uint16_t  num_addresses;
    uint16_t  reserved0;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_data_cube {
    uint16_t type;   /* dla_mem_type */
    int16_t address; /* offset to the actual IOVA in task.address_list */

    uint32_t offset; /* offset within address */
    uint32_t size;

    /* cube dimensions */
    uint16_t batch;
    uint16_t width;
    uint16_t height;

    uint16_t channel;
    uint16_t reserved0;

    /* stride information */
    uint32_t line_stride;
    uint32_t surf_stride;

    /* For Rubik only */
    uint32_t plane_stride;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_consumer {
    int16_t index;
    uint8_t event;
    uint8_t res;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_common_op_desc {
    int16_t index; /* set by ucode */
    int8_t  roi_index;
    uint8_t op_type;
    uint8_t dependency_count;
    uint8_t reserved0[3]; /* esim_tool uses reserved0[2] to save offset of op_index */
    struct dla_consumer consumers[HW_OP_NUM];
    struct dla_consumer fused_parent;
} __attribute__((packed, aligned(ALIGNMENT))); // 2176 bytes

struct dla_event_op_desc {
    int16_t index;         // a unique event op index in loadable
    int8_t submodel_type;  // 0-umd; 1-kmd; 2-e31; 3-p2p
    int8_t p2p_src;
    int8_t p2p_dst;
} __attribute__((packed, aligned(ALIGNMENT)));

#define EVENT_OP_TENSOR_NUM 8
struct event_surface_desc {
    struct dla_data_cube data[EVENT_OP_TENSOR_NUM];
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_cvt_param {
    int16_t scale;
    uint8_t truncate;
    uint8_t enable;

    int32_t offset;
} __attribute__((packed, aligned(ALIGNMENT)));

struct npu_edma_surface_desc {
    struct dla_data_cube src_data;
    struct dla_data_cube dst_data;
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

#define KERNEL_NAME_MAXLEN 128
#define KERNEL_LIB_NAME_MAXLEN 128
#define BUFFER_CNT_MAXSIZE 64

struct dsp_op_desc {
    /* *
     * Total byte size of this data structure.
     * */
    uint32_t total_size; /* size in op_config data blob */
    /* *
     * The authoritative name of the operator.
     * */
    char operator_name[KERNEL_NAME_MAXLEN];
    /* *
     * Specify total number of parameter buffers.
     * */
    uint32_t buffer_cnt_cfg;
    /* *
     * Specify total number of input buffers.
     * */
    uint32_t buffer_cnt_input;
    /* *
     * Specify total number of output buffers.
     * */
    uint32_t buffer_cnt_output;
    /* *
     * Specify the byte size of each buffer. This is an array describing the
     * size of each buffer including parameter, input and output buffers. They
     * are sequentially placed in this array.
     * */
    uint32_t buffer_size[BUFFER_CNT_MAXSIZE];
    /* *
     * This is a variable length field which holds parameter information. All
     * parameter buffers are sequentially laid out in this field.
     * */
    // char param_data[0];

    uint32_t dsp_core_id;
    uint32_t mem_id;
    uint32_t offset;  /* offset into op_config data blob */
} __attribute__((packed, aligned(ALIGNMENT)));

#define DSP_KERNEL_MAX_IN_TENSOR_NUM 8
#define DSP_KERNEL_MAX_OUT_TENSOR_NUM 8
#define DSP_KERNEL_MAX_INOUT_TENSOR_NUM (DSP_KERNEL_MAX_IN_TENSOR_NUM + DSP_KERNEL_MAX_OUT_TENSOR_NUM)
struct dsp_surface_desc {
    struct dla_data_cube src_data[DSP_KERNEL_MAX_IN_TENSOR_NUM];
    struct dla_data_cube dst_data[DSP_KERNEL_MAX_OUT_TENSOR_NUM];
} __attribute__((packed, aligned(ALIGNMENT)));

struct conv_mapping_info {
    uint32_t F3, G3, N3, M3, E4, C3;
    uint32_t G2, N2, C2, E3, R3, M2;
    uint32_t E1, R1, CV;
    uint32_t E0, F0, S, GMF, CMF, MMF;
    uint32_t GF, MF, CF;

    uint32_t G1_X, N1_X, M1_X, E2_X;
    uint32_t G1_Y, N1_Y, M1_Y, E2_Y, R2, C1;
} __attribute__((packed, aligned(ALIGNMENT)));

struct soft_conv_info {
    struct conv_mapping_info mapping_info;
    uint32_t first_level;
    uint32_t g, n, c, ofm_c0;
    uint32_t psum_trunc;
    uint8_t strides[2];
    uint8_t padding[4];
    uint8_t csc_format;
    uint8_t reserved;
    uint32_t ifmap_offset;
    uint32_t ifmap_cube_stride;
    uint32_t ifmap_surface_stride;
    uint32_t real_h;
    uint32_t real_w;
} __attribute__((packed, aligned(ALIGNMENT)));

#define CONV_CONFIG_MAX_SIZE (1536)
struct npu_conv_op_desc {
    char conv_config_data[CONV_CONFIG_MAX_SIZE];
    struct dla_cvt_param in_cvt;  /* input converter parameters */
    struct dla_cvt_param out_cvt; /* output converter parameters, support truncate only */
    struct soft_conv_info soft_conv_config;
    uint8_t src_precision;
    uint8_t dst_precision;
} __attribute__((packed, aligned(ALIGNMENT)));

struct npu_conv_surface_desc {
    /* Data cube */
    struct dla_data_cube weight_data;
    struct dla_data_cube wmb_data;
    struct dla_data_cube wgs_data;
    struct dla_data_cube src_data;
    struct dla_data_cube dst_data;
    /*
     * u_addr = input_data.source_addr + offset_u
     * this field should be set when YUV is not interleave format
     * */
    int64_t offset_u;

    /* line stride for 2nd plane, must be 32bytes aligned */
    uint32_t in_line_uv_stride;
} __attribute__((packed, aligned(ALIGNMENT)));

union dla_operation_container {
    struct npu_edma_op_desc edma_op;
    struct npu_conv_op_desc npu_conv_op;  //! add for npu conv desc
    //struct dla_sdp_op_desc sdp_op;
    //struct dla_pdp_op_desc pdp_op;
    struct dsp_op_desc dsp_op;
    //struct dla_rubik_op_desc rubik_op;
    //struct dla_event_op_desc event_op;
    //struct hae_op_desc hae_op;
    //struct gpu_op_desc gpu_op;
    //struct cpu_op_desc cpu_op;
};

union dla_surface_container {
    struct npu_edma_surface_desc edma_surface;
    struct npu_conv_surface_desc conv_surface;
    //struct dla_sdp_surface_desc sdp_surface;
    //struct dla_pdp_surface_desc pdp_surface;
    //struct dla_rubik_surface_desc rubik_surface;
    struct dsp_surface_desc dsp_surface;

    //struct event_surface_desc event_surface;
    //struct hae_surface_desc hae_surface;
    //struct gpu_surface_desc gpu_surface;
    //struct cpu_surface_desc cpu_surface;
};


/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

void *mmap_addrs[1024];

static void oflag_str(int oflag, char *buf, size_t buf_size) {
    int acc = oflag & O_ACCMODE;
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "%s",
                    acc == O_RDONLY ? "O_RDONLY" :
                    acc == O_WRONLY ? "O_WRONLY" :
                    acc == O_RDWR  ? "O_RDWR"   : "O_ACCMODE?");
#define OFLAG_BIT(f) if ((oflag & f) && pos < (int)buf_size) pos += snprintf(buf + pos, buf_size - pos, "|" #f)
    OFLAG_BIT(O_CREAT);
    OFLAG_BIT(O_EXCL);
    OFLAG_BIT(O_NOCTTY);
    OFLAG_BIT(O_TRUNC);
    OFLAG_BIT(O_APPEND);
    OFLAG_BIT(O_NONBLOCK);
    OFLAG_BIT(O_DSYNC);
    OFLAG_BIT(O_SYNC);
    OFLAG_BIT(O_CLOEXEC);
    OFLAG_BIT(O_DIRECTORY);
    OFLAG_BIT(O_NOFOLLOW);
    OFLAG_BIT(O_TMPFILE);
    OFLAG_BIT(O_PATH);
    OFLAG_BIT(O_LARGEFILE);
#undef OFLAG_BIT
}

static void prot_str(int prot, char *buf, size_t buf_size) {
    int pos = 0;
    if (prot == PROT_NONE) {
        snprintf(buf, buf_size, "PROT_NONE");
        return;
    }
#define PROT_BIT(f) if ((prot & f) && pos < (int)buf_size) \
        pos += snprintf(buf + pos, buf_size - pos, "%s" #f, pos ? "|" : "")
    PROT_BIT(PROT_READ);
    PROT_BIT(PROT_WRITE);
    PROT_BIT(PROT_EXEC);
#undef PROT_BIT
}

static void mmap_flags_str(int flags, char *buf, size_t buf_size) {
    int pos = 0;
    int type = flags & MAP_TYPE;
    pos += snprintf(buf, buf_size, "%s",
                    type == MAP_SHARED          ? "MAP_SHARED"          :
                    type == MAP_PRIVATE         ? "MAP_PRIVATE"         :
                    type == MAP_SHARED_VALIDATE ? "MAP_SHARED_VALIDATE" : "MAP_TYPE?");
#define MMAP_BIT(f) if ((flags & f) && pos < (int)buf_size) pos += snprintf(buf + pos, buf_size - pos, "|" #f)
    MMAP_BIT(MAP_FIXED);
    MMAP_BIT(MAP_ANONYMOUS);
    MMAP_BIT(MAP_GROWSDOWN);
    MMAP_BIT(MAP_DENYWRITE);
    MMAP_BIT(MAP_EXECUTABLE);
    MMAP_BIT(MAP_LOCKED);
    MMAP_BIT(MAP_NORESERVE);
    MMAP_BIT(MAP_POPULATE);
    MMAP_BIT(MAP_NONBLOCK);
    MMAP_BIT(MAP_STACK);
    MMAP_BIT(MAP_HUGETLB);
    MMAP_BIT(MAP_SYNC);
    MMAP_BIT(MAP_FIXED_NOREPLACE);
#undef MMAP_BIT
}

// Function to get the path from a file descriptor
char* get_path_from_fd(int fd, char* buffer, size_t buf_size) {
    char proc_path[PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);
    ssize_t len = readlink(proc_path, buffer, buf_size - 1);
    
    if (len != -1) {
        buffer[len] = '\0';
        return buffer;
    } else {
        perror("readlink failed");
        return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Interceptors                                                       */
/* ------------------------------------------------------------------ */

int (*original_openat)(int, const char *, int);
int openat(int fd, const char *path, int oflag, ...) {
    if (!original_openat) original_openat = dlsym(RTLD_NEXT, "openat");
    int rval = original_openat(fd, path, oflag);
    char oflag_buf[256];
    oflag_str(oflag, oflag_buf, sizeof(oflag_buf));
    tid_printf("openat( %d , %s , %s ) --> %s%d\n", fd, path, oflag_buf, FD_STR(rval));
    return rval;
}

int (*original_open)(const char *, int);
int open(const char *path, int oflag, ...) {
    if (!original_open) original_open = dlsym(RTLD_NEXT, "open");
    int rval = original_open(path, oflag);
    char oflag_buf[256];
    oflag_str(oflag, oflag_buf, sizeof(oflag_buf));
    tid_printf("open( %s , %s ) --> %s%d\n", path, oflag_buf, FD_STR(rval));
    return rval;
}

int (*original_open64)(const char *, int);
int open64(const char *path, int oflag, ...) {
    if (!original_open64) original_open64 = dlsym(RTLD_NEXT, "open64");
    int rval = original_open64(path, oflag);
    char oflag_buf[256];
    oflag_str(oflag, oflag_buf, sizeof(oflag_buf));
    tid_printf("open64( %s , %s ) --> %s%d\n", path, oflag_buf, FD_STR(rval));
    return rval;
}

int (*original_close)(int);
int close(int fd) {
    char fname[PATH_MAX];
    get_path_from_fd(fd, fname, sizeof(fname));
    if (!original_close) original_close = dlsym(RTLD_NEXT, "close");
    int rval = original_close(fd);
    tid_printf("close( fd_%d (%s) ) --> %d\n", fd, fname, rval);
    return rval;
}

int (*original_dup)(int);
int dup(int oldfd) {
    if (!original_dup) original_dup = dlsym(RTLD_NEXT, "dup");
    int rval = original_dup(oldfd);
    tid_printf("dup( fd_%d ) --> %s%d\n", oldfd, FD_STR(rval));
    return rval;
}

int (*original_dup2)(int, int);
int dup2(int oldfd, int newfd) {
    if (!original_dup2) original_dup2 = dlsym(RTLD_NEXT, "dup2");
    int rval = original_dup2(oldfd, newfd);
    tid_printf("dup2( fd_%d , fd_%d ) --> %s%d\n", oldfd, newfd, FD_STR(rval));
    return rval;
}

int (*original_dup3)(int, int, int);
int dup3(int oldfd, int newfd, int flags) {
    if (!original_dup3) original_dup3 = dlsym(RTLD_NEXT, "dup3");
    int rval = original_dup3(oldfd, newfd, flags);
    tid_printf("dup3( fd_%d , fd_%d , 0x%x ) --> %s%d\n", oldfd, newfd, flags, FD_STR(rval));
    return rval;
}

int (*original_epoll_create1)(int);
int epoll_create1(int flags) {
    if (!original_epoll_create1) original_epoll_create1 = dlsym(RTLD_NEXT, "epoll_create1");
    int rval = original_epoll_create1(flags);
    tid_printf("epoll_create1( 0x%x ) --> %s%d\n", flags, FD_STR(rval));
    return rval;
}

int (*original_eventfd)(unsigned int, int);
int eventfd(unsigned int initval, int flags) {
    if (!original_eventfd) original_eventfd = dlsym(RTLD_NEXT, "eventfd");
    int rval = original_eventfd(initval, flags);
    tid_printf("eventfd( %u , 0x%x ) --> %s%d\n", initval, flags, FD_STR(rval));
    return rval;
}

void *(*original_mmap)(void *, size_t, int, int, int, off_t);
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!original_mmap) original_mmap = dlsym(RTLD_NEXT, "mmap");

    void *rval = original_mmap(addr, length, prot, flags, fd, offset);
    char prot_buf[64], flags_buf[256];
    prot_str(prot, prot_buf, sizeof(prot_buf));
    mmap_flags_str(flags, flags_buf, sizeof(flags_buf));
    tid_printf("mmap( %p , %zu , %s , %s , fd_%d , %ld ) --> %p\n",
           addr, length, prot_buf, flags_buf, fd, (long)offset, rval);
    mmap_addrs[fd] = rval;
    return rval;
}

int (*original_ioctl)(int, unsigned long, ...);
int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void *argp = va_arg(args, void *);
    va_end(args);
    char fname[PATH_MAX];

    get_path_from_fd(fd, fname, sizeof(fname));

    if (!original_ioctl) {
        original_ioctl = dlsym(RTLD_NEXT, "ioctl");
    }

    tid_printf("ioctl( fd_%d (%s) , request=0x%lx , argp=0x%llx )\n", fd, fname, request, (unsigned long long)argp);

    if (strncmp(fname, "/dev/dma_heap/", 14) == 0) {
        if (request == DMA_HEAP_IOCTL_ALLOC) {
            struct dma_heap_allocation_data *arg = (struct dma_heap_allocation_data*) argp;
            char fd_flags_buf[256];
            oflag_str(arg->fd_flags, fd_flags_buf, sizeof(fd_flags_buf));
            printf("    DMA_HEAP_IOCTL_ALLOC\n");
            printf("    len=%llu\n", (unsigned long long)arg->len);
            printf("    fd_flags=%s\n", fd_flags_buf);
            printf("    heap_flags=0x%llx%s\n", (unsigned long long)arg->heap_flags,
                   (arg->heap_flags & HEAP_FLAGS_SPRAM_FORCE_CONTIGUOUS) ? " (HEAP_FLAGS_SPRAM_FORCE_CONTIGUOUS)" : "");
                        
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval==0) printf("    fd=fd_%u\n", arg->fd);
            return rval;
        }
    }

    if (strncmp(fname, "/dmabuf:", 8) == 0) {
        if (request == DMA_BUF_IOCTL_SYNC) {
            struct dma_buf_sync *arg = (struct dma_buf_sync*) argp;
            printf("    DMA_BUF_IOCTL_SYNC\n");
            printf("    flags=0x%llx (%s|%s)\n",
                   (unsigned long long)arg->flags,
                   (arg->flags & DMA_BUF_SYNC_RW) == DMA_BUF_SYNC_RW   ? "DMA_BUF_SYNC_RW"    :
                   (arg->flags & DMA_BUF_SYNC_WRITE)                    ? "DMA_BUF_SYNC_WRITE"  :
                   (arg->flags & DMA_BUF_SYNC_READ)                     ? "DMA_BUF_SYNC_READ"   : "0",
                   (arg->flags & DMA_BUF_SYNC_END)                      ? "DMA_BUF_SYNC_END"    : "DMA_BUF_SYNC_START");
        } else if (request == DMA_BUF_SET_NAME_A || request == DMA_BUF_SET_NAME_B) {
            printf("    DMA_BUF_SET_NAME\n");
            printf("    name=\"%s\"\n", (const char*)argp);
        }
        int rval = call_ioctl(fd, request, argp);
        printf("    ---> %d\n", rval);
        return rval;
    }

    if (strcmp(fname, "/dev/mmz_vb") == 0) {
        if (request == MMZ_VB_IOCTL_SET_CFG) {
            struct esVB_SET_CFG_CMD_S *arg = (struct esVB_SET_CFG_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_SET_CFG\n");
            printf("    uid=%u\n", arg->CfgReq.uid);
            printf("    cfg.poolCnt=%u\n", arg->CfgReq.cfg.poolCnt);
            for (u32 i = 0; i < arg->CfgReq.cfg.poolCnt && i < ES_VB_MAX_MOD_POOL; i++) {
                struct esVB_POOL_CONFIG_S *p = &arg->CfgReq.cfg.poolCfgs[i];
                printf("    poolCfgs[%u]: blkSize=%llu blkCnt=%u enRemapMode=%u mmzName=\"%s\"\n",
                       i, (unsigned long long)p->blkSize, p->blkCnt, p->enRemapMode, p->mmzName);
            }
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == MMZ_VB_IOCTL_GET_CFG) {
            struct esVB_GET_CFG_CMD_S *arg = (struct esVB_GET_CFG_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_GET_CFG\n");
            printf("    uid=%u\n", arg->req.uid);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) {
                printf("    cfg.poolCnt=%u\n", arg->rsp.cfg.poolCnt);
                for (u32 i = 0; i < arg->rsp.cfg.poolCnt && i < ES_VB_MAX_MOD_POOL; i++) {
                    struct esVB_POOL_CONFIG_S *p = &arg->rsp.cfg.poolCfgs[i];
                    printf("    poolCfgs[%u]: blkSize=%llu blkCnt=%u enRemapMode=%u mmzName=\"%s\"\n",
                           i, (unsigned long long)p->blkSize, p->blkCnt, p->enRemapMode, p->mmzName);
                }
            }
            return rval;
        } else if (request == MMZ_VB_IOCTL_INIT_CFG) {
            struct esVB_INIT_CFG_CMD_S *arg = (struct esVB_INIT_CFG_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_INIT_CFG\n");
            printf("    uid=%u\n", arg->req.uid);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == MMZ_VB_IOCTL_UNINIT_CFG) {
            struct esVB_UNINIT_CFG_CMD_S *arg = (struct esVB_UNINIT_CFG_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_UNINIT_CFG\n");
            printf("    uid=%u\n", arg->req.uid);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == MMZ_VB_IOCTL_CREATE_POOL) {
            struct esVB_CREATE_POOL_CMD_S *arg = (struct esVB_CREATE_POOL_CMD_S*) argp;
            struct esVB_POOL_CONFIG_S *p = &arg->PoolReq.req;
            printf("    MMZ_VB_IOCTL_CREATE_POOL\n");
            printf("    blkSize=%llu blkCnt=%u enRemapMode=%u mmzName=\"%s\"\n",
                   (unsigned long long)p->blkSize, p->blkCnt, p->enRemapMode, p->mmzName);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("    PoolId=%u\n", arg->PoolResp.PoolId);
            return rval;
        } else if (request == MMZ_VB_IOCTL_DESTORY_POOL) {
            struct esVB_DESTORY_POOL_CMD_S *arg = (struct esVB_DESTORY_POOL_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_DESTORY_POOL\n");
            printf("    PoolId=%u\n", arg->req.PoolId);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("    Result=%u\n", arg->rsp.Result);
            return rval;
        } else if (request == MMZ_VB_IOCTL_GET_BLOCK) {
            struct esVB_GET_BLOCK_CMD_S *arg = (struct esVB_GET_BLOCK_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_GET_BLOCK\n");
            printf("    uid=%u poolId=%u blkSize=%llu mmzName=\"%s\"\n",
                   arg->getBlkReq.uid, arg->getBlkReq.poolId,
                   (unsigned long long)arg->getBlkReq.blkSize, arg->getBlkReq.mmzName);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0)
                printf("    actualBlkSize=%llu fd=%s%d nr=%d\n",
                       (unsigned long long)arg->getBlkResp.actualBlkSize,
                       FD_STR(arg->getBlkResp.fd), arg->getBlkResp.nr);
            return rval;
        } else if (request == MMZ_VB_IOCTL_RETRIEVE_MEM_NODE) {
            struct esVB_RETRIEVE_MEM_NODE_CMD_S *arg = (struct esVB_RETRIEVE_MEM_NODE_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_RETRIEVE_MEM_NODE\n");
            printf("    fd=%s%d cpu_vaddr=%p\n", FD_STR(arg->fd), arg->cpu_vaddr);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("    numa_node=%d\n", (int)arg->numa_node);
            return rval;
        } else if (request == MMZ_VB_IOCTL_DMABUF_REFCOUNT) {
            struct esVB_DMABUF_REFCOUNT_CMD_S *arg = (struct esVB_DMABUF_REFCOUNT_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_DMABUF_REFCOUNT\n");
            printf("    fd=%s%d\n", FD_STR(arg->fd));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("    refCnt=%llu\n", (unsigned long long)arg->refCnt);
            return rval;
        } else if (request == MMZ_VB_IOCTL_DMABUF_SIZE) {
            struct esVB_DMABUF_SIZE_CMD_S *arg = (struct esVB_DMABUF_SIZE_CMD_S*) argp;
            printf("    MMZ_VB_IOCTL_DMABUF_SIZE\n");
            printf("    fd=%s%d\n", FD_STR(arg->fd));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("    size=%llu\n", (unsigned long long)arg->size);
            return rval;
        } else if (request == MMZ_VB_IOCTL_FLUSH_ALL) {
            printf("    MMZ_VB_IOCTL_FLUSH_ALL\n");
            printf("    flags=%d\n", *(int*)argp);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        }
    }

    #define MODNAME(t) ((t)==0?"vc8000e":(t)==1?"cutree":(t)==2?"vc8000d":(t)==3?"jpege":(t)==4?"jpegd":"?")
    if (strcmp(fname, "/dev/es_vdec") == 0) {
        if (request == HANTRODEC_IOCGHWIOSIZE) {
            struct regsize_desc *arg = (struct regsize_desc*) argp;
            printf("    HANTRODEC_IOCGHWIOSIZE\n");
            printf("    slice=%u id=%u type=%u\n", arg->slice, arg->id, arg->type);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("  size=%u\n", arg->size);
            return rval;
        } else if (request == HANTRODEC_IOX_SUBSYS) {
            printf("    HANTRODEC_IOX_SUBSYS\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) {
                struct subsys_desc *arg = (struct subsys_desc*) argp;
                printf("    subsys_num=%u subsys_vcmd_num=%u\n", arg->subsys_num, arg->subsys_vcmd_num);
            }
            return rval;
        } else if (request == HANTRO_VCMD_IOCH_GET_CMDBUF_PARAMETER) {
            printf("    HANTRO_VCMD_IOCH_GET_CMDBUF_PARAMETER\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) {
                struct cmdbuf_mem_parameter *arg = (struct cmdbuf_mem_parameter*) argp;
                printf("    phy_cmdbuf_addr=0x%llx\n", (unsigned long long)arg->phy_cmdbuf_addr);
                printf("    cmdbuf_total_size=%u  cmdbuf_unit_size=%u\n", arg->cmdbuf_total_size, arg->cmdbuf_unit_size);
                printf("    phy_status_cmdbuf_addr=0x%llx\n", (unsigned long long)arg->phy_status_cmdbuf_addr);
                printf("    status_cmdbuf_total_size=%u  status_cmdbuf_unit_size=%u\n", arg->status_cmdbuf_total_size, arg->status_cmdbuf_unit_size);
            }
            return rval;
        } else if (request == HANTRO_VCMD_IOCH_GET_VCMD_PARAMETER) {
            struct config_parameter *arg = (struct config_parameter*) argp;
            printf("    HANTRO_VCMD_IOCH_GET_VCMD_PARAMETER\n");
            printf("    module_type=%u (%s)\n", arg->module_type, MODNAME(arg->module_type));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) {
                printf("    vcmd_core_num=%u\n", arg->vcmd_core_num);
                printf("    vcmd_hw_version_id=0x%x\n", arg->vcmd_hw_version_id);
                if (arg->submodule_main_addr    != 0xffff) printf("    submodule_main=0x%x\n",     arg->submodule_main_addr);
                if (arg->submodule_dec400_addr  != 0xffff) printf("    submodule_dec400=0x%x\n",   arg->submodule_dec400_addr);
                if (arg->submodule_L2Cache_addr != 0xffff) printf("    submodule_L2Cache=0x%x\n",  arg->submodule_L2Cache_addr);
                if (arg->submodule_MMU_addr     != 0xffff) printf("    submodule_MMU=0x%x\n",      arg->submodule_MMU_addr);
                if (arg->submodule_MMUWrite_addr!= 0xffff) printf("    submodule_MMUWrite=0x%x\n", arg->submodule_MMUWrite_addr);
                if (arg->submodule_axife_addr   != 0xffff) printf("    submodule_axife=0x%x\n",    arg->submodule_axife_addr);
            }
            return rval;
        } else if (request == HANTRO_VCMD_IOCH_RESERVE_CMDBUF) {
            struct exchange_parameter *arg = (struct exchange_parameter*) argp;
            printf("    HANTRO_VCMD_IOCH_RESERVE_CMDBUF\n");
            printf("    executing_time=%llu\n", (unsigned long long)arg->executing_time);
            printf("    module_type=%u (%s)\n", arg->module_type, MODNAME(arg->module_type));
            printf("    cmdbuf_size=%u priority=%u nid=%u\n", arg->cmdbuf_size, arg->priority, arg->nid);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("    cmdbuf_id=%u core_id=%u\n", arg->cmdbuf_id, arg->core_id);
            return rval;
        } else if (request == HANTRO_VCMD_IOCH_LINK_RUN_CMDBUF) {
            printf("    HANTRO_VCMD_IOCH_LINK_RUN_CMDBUF\n");
            printf("    cmdbuf_id=%u\n", *(u16*)argp);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == HANTRO_VCMD_IOCH_WAIT_CMDBUF) {
            printf("    HANTRO_VCMD_IOCH_WAIT_CMDBUF\n");
            printf("    cmdbuf_id=%u\n", *(u16*)argp);
            int rval = call_ioctl(fd, request, argp);
            tid_printf("ioctl( fd_%d (%s) , request=0x%lx , argp=0x%llx ) --> %d\n", fd, fname, request, (unsigned long long)argp, rval);
            return rval;
        } else if (request == HANTRO_VCMD_IOCH_RELEASE_CMDBUF) {
            printf("    HANTRO_VCMD_IOCH_RELEASE_CMDBUF\n");
            printf("    cmdbuf_id=%u\n", *(u16*)argp);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == HANTRODEC_IOC_DMA_HEAP_GET_IOVA) {
            struct dmabuf_cfg *arg = (struct dmabuf_cfg*) argp;
            printf("    HANTRODEC_IOC_DMA_HEAP_GET_IOVA\n");
            printf("    dmabuf_fd=%s%d\n", FD_STR(arg->dmabuf_fd));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) printf("    iova=0x%lx\n", arg->iova);
            return rval;
        } else if (request == HANTRODEC_IOC_DMA_HEAP_PUT_IOVA) {
            printf("    HANTRODEC_IOC_DMA_HEAP_PUT_IOVA\n");
            printf("    dmabuf_fd=%s%d\n", FD_STR(*(int*)argp));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        }
    }
    #undef MODNAME


    if (strncmp(fname, "/dev/npu", 8) == 0) {
        if (request == ES_NPU_IOCTL_PREPARE_DMA_BUF) {
            ES_DEV_BUF_S *arg = (ES_DEV_BUF_S*) argp;
            printf("    ES_NPU_IOCTL_PREPARE_DMA_BUF\n");
            printf("    memFd=%s%d\n", FD_STR((int)arg->memFd));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_NPU_IOCTL_UNPREPARE_DMA_BUF) {
            printf("    ES_NPU_IOCTL_UNPREPARE_DMA_BUF\n");
            printf("    fd=%s%d\n", FD_STR((int)*(u32*)argp));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_NPU_IOCTL_MODEL_UNLOAD) {
            struct win_ioctl_args *arg = (struct win_ioctl_args*) argp;
            printf("    ES_NPU_IOCTL_MODEL_UNLOAD\n");
            printf("    model_idx=%u\n", arg->model_idx);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_NPU_IOCTL_GET_SRAM_FD) {
            struct win_ioctl_args *arg = (struct win_ioctl_args*) argp;
            printf("    ES_NPU_IOCTL_GET_SRAM_FD\n");
            printf("    data=%lx (sram_info_t out ptr)\n", (u64)arg->data);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0) {
                sram_info_t *info = (sram_info_t*)(u64)arg->data;
                printf("    sram_info.fd=%s%d\n", FD_STR(info->fd));
                printf("    sram_info.size=%u\n", info->size);
            }
            return rval;
        } else if (request == ES_NPU_IOCTL_GET_EVENT) {
            struct win_ioctl_args *arg = (struct win_ioctl_args*) argp;
            printf("    ES_NPU_IOCTL_GET_EVENT\n");
            printf("    data=%lx (event_data out ptr)\n", (u64)arg->data);
            printf("    pret=%lx (event_ret out ptr)\n", (u64)arg->pret);
            //printf("------------------------ES_NPU_IOCTL_GET_EVENT-----pre-sleep-3\n");
            //fflush(stdout); sleep(3);
            //printf("------------------------ES_NPU_IOCTL_GET_EVENT-----pre-sleep-3-done\n");
            int rval = call_ioctl(fd, request, argp);
            //printf("------------------------ES_NPU_IOCTL_GET_EVENT-----post-sleep-3\n");
            //fflush(stdout); sleep(3);
            //printf("------------------------ES_NPU_IOCTL_GET_EVENT-----post-sleep-3-done\n");
            tid_printf("ioctl( fd_%d (%s) , request=0x%lx , argp=0x%llx ) --> %d\n", fd, fname, request, (unsigned long long)argp, rval);
            if (rval == 0) {
                union event_union *event     = (union event_union*)(u64)arg->data;
                printf("    event.event_sinks=[%d, %d, %d, %d]\n",
                       event->event_sinks[0], event->event_sinks[1],
                       event->event_sinks[2], event->event_sinks[3]);
                if (arg->pret != 0) {
                    union event_union *event_ret = (union event_union*)(u64)arg->pret;
                    printf("    event_ret.event_sinks=[%d, %d, %d, %d]\n",
                        event_ret->event_sinks[0], event_ret->event_sinks[1],
                        event_ret->event_sinks[2], event_ret->event_sinks[3]);
                }
            }
            return rval;
        } else if (request == ES_NPU_IOCTL_MODEL_LOAD) {
            printf("    ES_NPU_IOCTL_MODEL_LOAD\n");
            struct win_ioctl_args *arg = (struct win_ioctl_args*) argp;
            void *shm_addr = (void*)0;
            if (arg->shm_fd<1024)
                shm_addr=mmap_addrs[arg->shm_fd];
            printf("    shm_fd=%s%d (0x%lx) modelShmDesc_t\n", FD_STR((int)arg->shm_fd), (u64)shm_addr);
            printf("    pret=%p\n", (void*)arg->pret);
            if (shm_addr != 0) {
                modelShmDesc_t *mdl = (modelShmDesc_t*)shm_addr;
                printf("    modelShmDesc.kmdSubModelId=%u\n", mdl->kmdSubModelId);
                printf("    modelShmDesc.kmdNetworkAddrId=%u\n", mdl->kmdNetworkAddrId);
                printf("    modelShmDesc.dspFd=[%s%d, %s%d, %s%d, %s%d]\n", FD_STR(mdl->dspFd[0]), FD_STR(mdl->dspFd[1]), FD_STR(mdl->dspFd[2]), FD_STR(mdl->dspFd[3]));
                printf("    modelShmDesc.batch_num=%d\n", mdl->batch_num);
                printf("    modelShmDesc.addrList.numAddress=%u\n    modelShmDesc.addrList.addrDesc\n", mdl->addrList.numAddress);
                for (int i = 0; i < mdl->addrList.numAddress; i++) {
                    printf("    [%d]: devbuf.memFd=%s%d devbuf.offset=%lu devbuf.size=%lu flag=%s bindId=%d virtAddr=%lx memoryType=%u\n", i,
                                                                            FD_STR((int)mdl->addrList.addrDesc[i].devBuf.memFd),
                                                                            mdl->addrList.addrDesc[i].devBuf.offset,
                                                                            mdl->addrList.addrDesc[i].devBuf.size,
                                                                            MEM_FLAG(mdl->addrList.addrDesc[i].flag),
                                                                            mdl->addrList.addrDesc[i].bindId,
                                                                            (u64)mdl->addrList.addrDesc[i].virtAddr,
                                                                            mdl->addrList.addrDesc[i].memoryType);
                }

                struct dla_network_desc *ndesc = (struct dla_network_desc*) mdl->addrList.addrDesc[mdl->kmdNetworkAddrId].virtAddr;
                printf("\n    kmdNetwork @ modelShmDesc.addrList.addrDesc[%d]:\n", mdl->kmdNetworkAddrId);
                printf("        dla_network_desc.version=%d.%d.%d\n", ndesc->version.major_version, ndesc->version.minor_version, ndesc->version.subminor_version);
                printf("        dla_network_desc.operation_desc_index=%d\n", ndesc->operation_desc_index);
                printf("        dla_network_desc.surface_desc_index=%d\n", ndesc->surface_desc_index);
                printf("        dla_network_desc.dependency_graph_index=%d\n", ndesc->dependency_graph_index);
                printf("        dla_network_desc.lut_data_index=%d\n", ndesc->lut_data_index);
                printf("        dla_network_desc.op_config_index=%d\n", ndesc->op_config_index);
                printf("        dla_network_desc.num_operations=%u\n", ndesc->num_operations);
                printf("        dla_network_desc.num_event_ops=%u\n", ndesc->num_event_ops);
                printf("        dla_network_desc.num_luts=%u\n", ndesc->num_luts);
                printf("        dla_network_desc.num_addresses=%u\n", ndesc->num_addresses);
                
                int nops = ndesc->num_operations;
                #ifdef OP_PRINT_LIMIT
                if (nops > OP_PRINT_LIMIT) nops = OP_PRINT_LIMIT;
                #endif
                
                struct dla_common_op_desc *dep = (struct dla_common_op_desc*) mdl->addrList.addrDesc[ndesc->dependency_graph_index].virtAddr;
                printf("\n    dependency_graph @ modelShmDesc.addrList.addrDesc[%d]:\n    dla_common_op_desc\n", ndesc->dependency_graph_index);
                
                for (int i = 0; i < nops; i++) {
                    printf("        [%d]: index=%d roi=%d op_type=%u dep_count=%u fused_parent=%d/%u consumers=",
                           i, dep[i].index, dep[i].roi_index, dep[i].op_type, dep[i].dependency_count,
                           dep[i].fused_parent.index, dep[i].fused_parent.event);
                    for (int j = 0; j < HW_OP_NUM; j++) {
                        int16_t idx = dep[i].consumers[j].index;
                        uint8_t ev = dep[i].consumers[j].event;
                        if ((idx != -1) || (ev != 1))
                            printf(" %d:%d/%u", j, idx, ev);
                    }
                    printf(" (others: *:-1/1)\n");
                }

                printf("\n    operation_desc @ modelShmDesc.addrList.addrDesc[%d]:\n\n", ndesc->operation_desc_index);
                for (int i = 0; i < nops; i++) {
                    union dla_operation_container *op_desc = ((union dla_operation_container*) mdl->addrList.addrDesc[ndesc->operation_desc_index].virtAddr) + i;
                    switch (dep[i].op_type) {
                        case DLA_OP_EDMA: {
                            struct npu_edma_op_desc *edma = (struct npu_edma_op_desc*) op_desc;
                            printf("        [%d]:npu_edma_op_desc.input_c0_bytes=%u\n", i, edma->input_c0_bytes);
                            printf("        [%d]:npu_edma_op_desc.src: num_line=%u stride_line=%u num_surface=%u stride_surface=%u num_cube=%u stride_cube=%u num_colony=%u\n",
                                   i, edma->src_num_line, edma->src_stride_line_bytes,
                                   edma->src_num_surface, edma->src_stride_surface_bytes,
                                   edma->src_num_cube, edma->src_stride_cube_bytes, edma->src_num_colony);
                            printf("        [%d]:npu_edma_op_desc.output_c0_bytes=%u\n", i, edma->output_c0_bytes);
                            printf("        [%d]:npu_edma_op_desc.dst: num_line=%u stride_line=%u num_surface=%u stride_surface=%u num_cube=%u stride_cube=%u num_colony=%u\n\n",
                                   i, edma->dst_num_line, edma->dst_stride_line_bytes,
                                   edma->dst_num_surface, edma->dst_stride_surface_bytes,
                                   edma->dst_num_cube, edma->dst_stride_cube_bytes, edma->dst_num_colony);
                            break;
                        }
                        case DLA_OP_CONV: {
                            struct npu_conv_op_desc *conv = (struct npu_conv_op_desc*) op_desc;
                            /* conv_config_data[CONV_CONFIG_MAX_SIZE] is opaque HW register data, not printed */
                            printf("        [%d]:npu_conv_op_desc.in_cvt: scale=%d truncate=%u enable=%u offset=%d\n",
                                   i, conv->in_cvt.scale, conv->in_cvt.truncate, conv->in_cvt.enable, conv->in_cvt.offset);
                            printf("        [%d]:npu_conv_op_desc.out_cvt: scale=%d truncate=%u enable=%u offset=%d\n",
                                   i, conv->out_cvt.scale, conv->out_cvt.truncate, conv->out_cvt.enable, conv->out_cvt.offset);
                            printf("        [%d]:npu_conv_op_desc.soft_conv_config.mapping_info:"
                                   " F3=%u G3=%u N3=%u M3=%u E4=%u C3=%u G2=%u N2=%u C2=%u E3=%u R3=%u M2=%u"
                                   " E1=%u R1=%u CV=%u E0=%u F0=%u S=%u GMF=%u CMF=%u MMF=%u GF=%u MF=%u CF=%u"
                                   " G1_X=%u N1_X=%u M1_X=%u E2_X=%u G1_Y=%u N1_Y=%u M1_Y=%u E2_Y=%u R2=%u C1=%u\n",
                                   i,
                                   conv->soft_conv_config.mapping_info.F3,
                                   conv->soft_conv_config.mapping_info.G3,
                                   conv->soft_conv_config.mapping_info.N3,
                                   conv->soft_conv_config.mapping_info.M3,
                                   conv->soft_conv_config.mapping_info.E4,
                                   conv->soft_conv_config.mapping_info.C3,
                                   conv->soft_conv_config.mapping_info.G2,
                                   conv->soft_conv_config.mapping_info.N2,
                                   conv->soft_conv_config.mapping_info.C2,
                                   conv->soft_conv_config.mapping_info.E3,
                                   conv->soft_conv_config.mapping_info.R3,
                                   conv->soft_conv_config.mapping_info.M2,
                                   conv->soft_conv_config.mapping_info.E1,
                                   conv->soft_conv_config.mapping_info.R1,
                                   conv->soft_conv_config.mapping_info.CV,
                                   conv->soft_conv_config.mapping_info.E0,
                                   conv->soft_conv_config.mapping_info.F0,
                                   conv->soft_conv_config.mapping_info.S,
                                   conv->soft_conv_config.mapping_info.GMF,
                                   conv->soft_conv_config.mapping_info.CMF,
                                   conv->soft_conv_config.mapping_info.MMF,
                                   conv->soft_conv_config.mapping_info.GF,
                                   conv->soft_conv_config.mapping_info.MF,
                                   conv->soft_conv_config.mapping_info.CF,
                                   conv->soft_conv_config.mapping_info.G1_X,
                                   conv->soft_conv_config.mapping_info.N1_X,
                                   conv->soft_conv_config.mapping_info.M1_X,
                                   conv->soft_conv_config.mapping_info.E2_X,
                                   conv->soft_conv_config.mapping_info.G1_Y,
                                   conv->soft_conv_config.mapping_info.N1_Y,
                                   conv->soft_conv_config.mapping_info.M1_Y,
                                   conv->soft_conv_config.mapping_info.E2_Y,
                                   conv->soft_conv_config.mapping_info.R2,
                                   conv->soft_conv_config.mapping_info.C1);
                            printf("        [%d]:npu_conv_op_desc.soft_conv_config:"
                                   " first_level=%u g=%u n=%u c=%u ofm_c0=%u psum_trunc=%u"
                                   " strides=%u,%u padding=%u,%u,%u,%u csc_format=%u"
                                   " ifmap_offset=%u ifmap_cube_stride=%u ifmap_surface_stride=%u"
                                   " real_h=%u real_w=%u\n",
                                   i,
                                   conv->soft_conv_config.first_level,
                                   conv->soft_conv_config.g, conv->soft_conv_config.n, conv->soft_conv_config.c,
                                   conv->soft_conv_config.ofm_c0, conv->soft_conv_config.psum_trunc,
                                   conv->soft_conv_config.strides[0], conv->soft_conv_config.strides[1],
                                   conv->soft_conv_config.padding[0], conv->soft_conv_config.padding[1],
                                   conv->soft_conv_config.padding[2], conv->soft_conv_config.padding[3],
                                   conv->soft_conv_config.csc_format,
                                   conv->soft_conv_config.ifmap_offset,
                                   conv->soft_conv_config.ifmap_cube_stride,
                                   conv->soft_conv_config.ifmap_surface_stride,
                                   conv->soft_conv_config.real_h, conv->soft_conv_config.real_w);
                            printf("        [%d]:npu_conv_op_desc.src_precision=%u\n", i, conv->src_precision);
                            printf("        [%d]:npu_conv_op_desc.dst_precision=%u\n\n", i, conv->dst_precision);
                            break;
                        }
                        case DLA_KMD_OP_DSP_0:
                        case DLA_KMD_OP_DSP_1:
                        case DLA_KMD_OP_DSP_2:
                        case DLA_KMD_OP_DSP_3: {
                            struct dsp_op_desc *dsp = (struct dsp_op_desc*) op_desc;
                            printf("        [%d]:dsp_op_desc: total_size=%u operator_name=%s buffer_cnt_cfg=%u buffer_cnt_input=%u buffer_cnt_output=%u dsp_core_id=%u mem_id=%u offset=%u\n",
                                   i, dsp->total_size, dsp->operator_name,
                                   dsp->buffer_cnt_cfg, dsp->buffer_cnt_input, dsp->buffer_cnt_output,
                                   dsp->dsp_core_id, dsp->mem_id, dsp->offset);
                            printf("        [%d]:dsp_op_desc.buffer_size:", i);
                            for (int j = 0; j < (int)(dsp->buffer_cnt_cfg + dsp->buffer_cnt_input + dsp->buffer_cnt_output); j++)
                                printf(" [%d]=%u", j, dsp->buffer_size[j]);
                            printf("\n\n");
                            break;
                        }
                    }
                }

                printf("    surface_desc @ modelShmDesc.addrList.addrDesc[%d]:\n\n", ndesc->surface_desc_index);
                for (int i = 0; i < nops; i++) {
                    union dla_surface_container *surf_desc = ((union dla_surface_container*) mdl->addrList.addrDesc[ndesc->surface_desc_index].virtAddr) + i;
                    switch (dep[i].op_type) {
                        case DLA_OP_EDMA: {
                            struct npu_edma_surface_desc *surf = (struct npu_edma_surface_desc*) surf_desc;
                            printf("        [%d]:npu_edma_surface_desc.src_data: type=%u address=%d offset=%u size=%u batch=%u width=%u height=%u channel=%u line_stride=%u surf_stride=%u plane_stride=%u\n",
                                   i, surf->src_data.type, surf->src_data.address, surf->src_data.offset, surf->src_data.size,
                                   surf->src_data.batch, surf->src_data.width, surf->src_data.height, surf->src_data.channel,
                                   surf->src_data.line_stride, surf->src_data.surf_stride, surf->src_data.plane_stride);
                            printf("        [%d]:npu_edma_surface_desc.dst_data: type=%u address=%d offset=%u size=%u batch=%u width=%u height=%u channel=%u line_stride=%u surf_stride=%u plane_stride=%u\n\n",
                                   i, surf->dst_data.type, surf->dst_data.address, surf->dst_data.offset, surf->dst_data.size,
                                   surf->dst_data.batch, surf->dst_data.width, surf->dst_data.height, surf->dst_data.channel,
                                   surf->dst_data.line_stride, surf->dst_data.surf_stride, surf->dst_data.plane_stride);
                            break;
                        }
                        case DLA_OP_CONV: {
                            struct npu_conv_surface_desc *surf = (struct npu_conv_surface_desc*) surf_desc;
                            printf("        [%d]:npu_conv_surface_desc.weight_data: type=%u address=%d offset=%u size=%u width=%u height=%u channel=%u line_stride=%u surf_stride=%u plane_stride=%u\n",
                                   i, surf->weight_data.type, surf->weight_data.address, surf->weight_data.offset, surf->weight_data.size,
                                   surf->weight_data.width, surf->weight_data.height, surf->weight_data.channel,
                                   surf->weight_data.line_stride, surf->weight_data.surf_stride, surf->weight_data.plane_stride);
                            printf("        [%d]:npu_conv_surface_desc.wmb_data: type=%u address=%d offset=%u size=%u\n",
                                   i, surf->wmb_data.type, surf->wmb_data.address, surf->wmb_data.offset, surf->wmb_data.size);
                            printf("        [%d]:npu_conv_surface_desc.wgs_data: type=%u address=%d offset=%u size=%u\n",
                                   i, surf->wgs_data.type, surf->wgs_data.address, surf->wgs_data.offset, surf->wgs_data.size);
                            printf("        [%d]:npu_conv_surface_desc.src_data: type=%u address=%d offset=%u size=%u width=%u height=%u channel=%u line_stride=%u surf_stride=%u plane_stride=%u\n",
                                   i, surf->src_data.type, surf->src_data.address, surf->src_data.offset, surf->src_data.size,
                                   surf->src_data.width, surf->src_data.height, surf->src_data.channel,
                                   surf->src_data.line_stride, surf->src_data.surf_stride, surf->src_data.plane_stride);
                            printf("        [%d]:npu_conv_surface_desc.dst_data: type=%u address=%d offset=%u size=%u width=%u height=%u channel=%u line_stride=%u surf_stride=%u plane_stride=%u\n",
                                   i, surf->dst_data.type, surf->dst_data.address, surf->dst_data.offset, surf->dst_data.size,
                                   surf->dst_data.width, surf->dst_data.height, surf->dst_data.channel,
                                   surf->dst_data.line_stride, surf->dst_data.surf_stride, surf->dst_data.plane_stride);
                            printf("        [%d]:npu_conv_surface_desc.offset_u=%ld\n", i, surf->offset_u);
                            printf("        [%d]:npu_conv_surface_desc.in_line_uv_stride=%u\n\n", i, surf->in_line_uv_stride);
                            break;
                        }
                        case DLA_KMD_OP_DSP_0:
                        case DLA_KMD_OP_DSP_1:
                        case DLA_KMD_OP_DSP_2:
                        case DLA_KMD_OP_DSP_3: {
                            struct dsp_surface_desc *surf = (struct dsp_surface_desc*) surf_desc;
                            union dla_operation_container *op_desc_i = ((union dla_operation_container*) mdl->addrList.addrDesc[ndesc->operation_desc_index].virtAddr) + i;
                            struct dsp_op_desc *dsp = (struct dsp_op_desc*) op_desc_i;
                            for (int j = 0; j < (int)dsp->buffer_cnt_input; j++) {
                                printf("        [%d]:dsp_surface_desc.src_data[%d]: type=%u address=%d offset=%u size=%u width=%u height=%u channel=%u line_stride=%u surf_stride=%u plane_stride=%u\n",
                                       i, j, surf->src_data[j].type, surf->src_data[j].address, surf->src_data[j].offset, surf->src_data[j].size,
                                       surf->src_data[j].width, surf->src_data[j].height, surf->src_data[j].channel,
                                       surf->src_data[j].line_stride, surf->src_data[j].surf_stride, surf->src_data[j].plane_stride);
                            }
                            for (int j = 0; j < (int)dsp->buffer_cnt_output; j++) {
                                printf("        [%d]:dsp_surface_desc.dst_data[%d]: type=%u address=%d offset=%u size=%u width=%u height=%u channel=%u line_stride=%u surf_stride=%u plane_stride=%u\n",
                                       i, j, surf->dst_data[j].type, surf->dst_data[j].address, surf->dst_data[j].offset, surf->dst_data[j].size,
                                       surf->dst_data[j].width, surf->dst_data[j].height, surf->dst_data[j].channel,
                                       surf->dst_data[j].line_stride, surf->dst_data[j].surf_stride, surf->dst_data[j].plane_stride);
                            }
                            printf("\n");
                            break;
                        }
                    }
                }
            }
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            if (rval == 0)
                printf("    model_id=%ld\n", (s64) *((s64*)arg->pret));
            return rval;
        } else if (request == ES_NPU_IOCTL_TASK_SUBMIT) {
            struct win_ioctl_args *arg = (struct win_ioctl_args*) argp;
            printf("    ES_NPU_IOCTL_TASK_SUBMIT\n");
            printf("    model_idx=%d\n", arg->model_idx);
            printf("    tensor_size=%d\n", arg->tensor_size);
            printf("    io_tensors=%lx\n", (u64)arg->data);
            printf("    pret=%lx\n", (u64)arg->pret);
            printf("    hetero_cmd=%d\n", arg->hetero_cmd);
            printf("    frame_idx=%d\n", arg->frame_idx);
            printf("    dump_enable=%d\n", arg->dump_enable);
            addrDesc_t *io_tensors = (addrDesc_t *) arg->data;
            for (int i = 0; i < arg->tensor_size/sizeof(addrDesc_t); i++) {
                printf("    io_tensor[%d]: memFd=%lu offset=%lu size=%lu flag=%d bindId=%d virtAddr=%lx memoryType=%u\n",
                       i, io_tensors[i].devBuf.memFd, io_tensors[i].devBuf.offset,
                       io_tensors[i].devBuf.size, io_tensors[i].flag,
                       io_tensors[i].bindId, (u64)io_tensors[i].virtAddr,
                       io_tensors[i].memoryType);
            }
            int rval = call_ioctl(fd, request, argp);            
            printf("    ---> %d\n", rval);
            return rval;
        }
    }

    if (strncmp(fname, "/dev/es-dsp", 11) == 0) {
        if (request == DSP_IOCTL_UNLOAD_OP) {
            printf("    DSP_IOCTL_UNLOAD_OP\n");
            printf("    op_fd=%s%d\n", FD_STR(*(int*)argp));
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_LOAD_OP) {
            printf("    DSP_IOCTL_LOAD_OP\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_ALLOC) {
            printf("    DSP_IOCTL_ALLOC\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_FREE) {
            printf("    DSP_IOCTL_FREE\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_QUEUE) {
            printf("    DSP_IOCTL_QUEUE\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_QUEUE_NS) {
            printf("    DSP_IOCTL_QUEUE_NS\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_IMPORT) {
            printf("    DSP_IOCTL_IMPORT\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_ALLOC_COHERENT) {
            printf("    DSP_IOCTL_ALLOC_COHERENT\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_FREE_COHERENT) {
            printf("    DSP_IOCTL_FREE_COHERENT\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_REG_TASK) {
            printf("    DSP_IOCTL_REG_TASK\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_SUBMIT_TSK_ASYNC) {
            printf("    DSP_IOCTL_SUBMIT_TSK_ASYNC\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_PROCESS_REPORT) {
            printf("    DSP_IOCTL_PROCESS_REPORT\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_SUBMIT_TSK) {
            printf("    DSP_IOCTL_SUBMIT_TSK\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_SUBMIT_TSKS_ASYNC) {
            printf("    DSP_IOCTL_SUBMIT_TSKS_ASYNC\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_WAIT_IRQ) {
            printf("    DSP_IOCTL_WAIT_IRQ\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_QUERY_TASK) {
            printf("    DSP_IOCTL_QUERY_TASK\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_PREPARE_DMA) {
            printf("    DSP_IOCTL_PREPARE_DMA\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == DSP_IOCTL_UNPREPARE_DMA) {
            printf("    DSP_IOCTL_UNPREPARE_DMA\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        }
    }

    if (strncmp(fname, "/dev/es_dec", 11) == 0 ||
        strncmp(fname, "/dev/es_enc", 11) == 0 ||
        strncmp(fname, "/dev/es_bms", 11) == 0 ||
        strncmp(fname, "/dev/es_vps", 11) == 0 ||
        strncmp(fname, "/dev/es_vo",  10) == 0) {
        /* es-media-ext framework: module device (magic 'm'), channel device (magic 'c') */
        if (request == ES_MOD_IOC_GET_EVENT) {
            printf("    ES_MOD_IOC_GET_EVENT\n");
            int rval = call_ioctl(fd, request, argp);
            es_module_event_t *ev = (es_module_event_t *)argp;
            printf("    ES_MOD_IOC_GET_EVENT\n    ---> %d\n", rval);
            if (rval == 0)
                printf("    id=%d token=%u chn=%u/%u\n", ev->id, ev->token,
                       ev->chn.group, ev->chn.channel);
            return rval;
        } else if (request == ES_MOD_IOC_PROC_SEND_MODULE) {
            printf("    ES_MOD_IOC_PROC_SEND_MODULE\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_MOD_IOC_PROC_SEND_GRP_TITLE) {
            printf("    ES_MOD_IOC_PROC_SEND_GRP_TITLE\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_MOD_IOC_PROC_SEND_GRP_DATA) {
            printf("    ES_MOD_IOC_PROC_SEND_GRP_DATA\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_MOD_IOC_PUB_USER) {
            printf("    ES_MOD_IOC_PUB_USER\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_MOD_IOC_PROC_SET_SECTION) {
            printf("    ES_MOD_IOC_PROC_SET_SECTION\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_MOD_IOC_PROC_SET_TIMEOUT) {
            unsigned int *timeout = (unsigned int *)argp;
            printf("    ES_MOD_IOC_PROC_SET_TIMEOUT\n    timeout=%u\n", *timeout);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        /* channel device: /dev/es_vps1 (magic 'c') */
        } else if (request == ES_CHN_IOC_COUNT_ADD) {
            unsigned int *val = (unsigned int *)argp;
            printf("    ES_CHN_IOC_COUNT_ADD\n    val=%u\n", *val);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d val=%u\n", rval, *val);
            return rval;
        } else if (request == ES_CHN_IOC_COUNT_SUB) {
            unsigned int *val = (unsigned int *)argp;
            printf("    ES_CHN_IOC_COUNT_SUB\n    val=%u\n", *val);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d val=%u\n", rval, *val);
            return rval;
        } else if (request == ES_CHN_IOC_COUNT_GET) {
            printf("    ES_CHN_IOC_COUNT_GET\n");
            int rval = call_ioctl(fd, request, argp);
            unsigned int *val = (unsigned int *)argp;
            printf("    ---> %d val=%u\n", rval, *val);
            return rval;
        } else if (request == ES_CHN_IOC_ASSIGN_CHANNEL) {
            es_channel_t *chn = (es_channel_t *)argp;
            printf("    ES_CHN_IOC_ASSIGN_CHANNEL\n    grp=%u chn=%u\n", chn->group, chn->channel);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_CHN_IOC_UNASSIGN_CHANNEL) {
            es_channel_t *chn = (es_channel_t *)argp;
            printf("    ES_CHN_IOC_UNASSIGN_CHANNEL\n    grp=%u chn=%u\n", chn->group, chn->channel);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_CHN_IOC_WAKEUP_COUNT_SET) {
            unsigned int *val = (unsigned int *)argp;
            printf("    ES_CHN_IOC_WAKEUP_COUNT_SET\n    val=%u\n", *val);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ES_CHN_IOC_WAKEUP_COUNT_GET) {
            printf("    ES_CHN_IOC_WAKEUP_COUNT_GET\n");
            int rval = call_ioctl(fd, request, argp);
            unsigned int *val = (unsigned int *)argp;
            printf("    ---> %d val=%u\n", rval, *val);
            return rval;
        }
    }

    if (strcmp(fname, "/dev/es_hae") == 0) {
        if (request == IOCTL_GCHAL_INTERFACE || request == IOCTL_GCHAL_PROFILER_INTERFACE) {
            es_hae_driver_args_t *dargs = (es_hae_driver_args_t *)argp;
            es_hae_iface_hdr_t *iface = (es_hae_iface_hdr_t *)(uintptr_t)dargs->InputBuffer;
            const char *req_name = (request == IOCTL_GCHAL_INTERFACE) ? "GCHAL_INTERFACE" : "GCHAL_PROFILER_INTERFACE";
            printf("    %s\n", req_name);
            printf("    cmd=%s dev=%u core=%u hwtype=%d engine=%d\n",
                   gcvHAL_cmd_str(iface->command),
                   iface->devIndex, iface->coreIndex,
                   iface->hardwareType, iface->engine);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d status=%d\n", rval, iface->status);
            return rval;
        } else if (request == IOCTL_GCHAL_TERMINATE) {
            printf("    GCHAL_TERMINATE\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == IOCTL_ESW_ALLOC_IOVA) {
            es_hae_dmabuf_cfg_t *cfg = (es_hae_dmabuf_cfg_t *)argp;
            printf("    ESW_ALLOC_IOVA\n");
            printf("    dmabuf_fd=%d\n", cfg->fd);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d iova=0x%lx error=%d\n", rval, (uint64_t)cfg->iova, cfg->error);
            return rval;
        } else if (request == IOCTL_ESW_FREE_IOVA) {
            es_hae_dmabuf_cfg_t *cfg = (es_hae_dmabuf_cfg_t *)argp;
            printf("    ESW_FREE_IOVA\n");
            printf("    dmabuf_fd=%d iova=0x%lx\n", cfg->fd, (uint64_t)cfg->iova);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        }
    }

    if (strcmp(fname, "/dev/es_memcp") == 0) {
        if (request == ESW_CMDQ_ADD_TASK) {
            struct esw_memcp_f2f_cmd *cmd = (struct esw_memcp_f2f_cmd *)argp;
            printf("    ESW_CMDQ_ADD_TASK\n");
            printf("    src_fd=%s%d src_offset=%u\n", FD_STR(cmd->src_fd), cmd->src_offset);
            printf("    dst_fd=%s%d dst_offset=%u\n", FD_STR(cmd->dst_fd), cmd->dst_offset);
            printf("    len=%zu timeout=%d\n", cmd->len, cmd->timeout);
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ESW_CMDQ_SYNC) {
            printf("    ESW_CMDQ_SYNC\n");
            int rval = call_ioctl(fd, request, argp);
            printf("    ---> %d\n", rval);
            return rval;
        } else if (request == ESW_CMDQ_QUERY) {
            printf("    ESW_CMDQ_QUERY\n");
            int rval = call_ioctl(fd, request, argp);
            struct esw_cmdq_query *q = (struct esw_cmdq_query *)argp;
            printf("    ---> %d status=%d task_count=%d last_error=%d\n",
                   rval, q->status, q->task_count, q->last_error);
            return rval;
        }
    }

    printf("    TODO\n");
    int rval = call_ioctl(fd, request, argp);
    tid_printf("ioctl( fd_%d (%s) , request=0x%lx , argp=0x%llx ) ---> %d\n", fd, fname, request, (unsigned long long)argp, rval);
    return rval;
}
