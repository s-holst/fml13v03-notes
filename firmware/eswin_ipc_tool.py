#!/usr/bin/env -S uv run
"""
SCPU IPC Client for ESWIN EIC770x
=================================

Uses the eswin-ipc-scpu kernel driver which exposes /dev/eswin_ipc_misc_dev*.
Among various ioctl commands, the driver supports sending and receiving
messages to SCPU via mbox0 (MPU->SCPU) and mbox1 (SCPU->MCPU) with the command
SCPU_IOC_MESSAGE_COMMUNICATION. The messages are received by the service loop
running on SCPU after boot. It exposes various services SRVC_TYPE_* for
crytography (signing, hashing, encryption/decryption, random number generation
using the SPAcc, PKA, and TRNG peripherals accessible by SCPU), access to OTP
memory, PMP configuration, and generic r/w access to any memory and mmio in
the SCPU memory space (SRVC_TYPE_BASIC_IO).

Relevant source code of the eswin-ipc-scpu driver:
https://github.com/DC-DeepComputing/fml13v03_linux/blob/fml13v03-6.6.92/include/uapi/linux/eswin-ipc-scpu.h
https://github.com/DC-DeepComputing/fml13v03_linux/blob/fml13v03-6.6.92/drivers/crypto/eswin/eswin-ipc-scpu.c

Relevant source code for the eswin-mailbox driver (used by eswin-ipc-scpu):
https://github.com/DC-DeepComputing/fml13v03_linux/blob/fml13v03-6.6.92/include/linux/mailbox/eswin-mailbox.h
https://github.com/DC-DeepComputing/fml13v03_linux/blob/fml13v03-6.6.92/drivers/mailbox/eswin-mailbox.c
"""

import os, fcntl, struct, argparse

# from https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/asm-generic/ioctl.h

_IOC_READ  = 2
_IOC_WRITE = 1
def _IOC(dir_, type_, nr, size): return (dir_ << 30) | ((size & 0x3FFF) << 16) | (type_ << 8) | nr

# from https://github.com/DC-DeepComputing/fml13v03_linux/blob/fml13v03-6.6.92/include/uapi/linux/eswin-ipc-scpu.h

SCPU_IOC_MAGIC = ord('S')
SCPU_IOC_CPU_NID = _IOC(_IOC_READ, SCPU_IOC_MAGIC, 0x5, 8)  # sizeof(cipher_get_nid_req_t) = 8
SCPU_IOC_MESSAGE_COMMUNICATION = _IOC(_IOC_READ|_IOC_WRITE, SCPU_IOC_MAGIC, 0xa, 2192)  # sizeof(cipher_create_handle_req_t) = 2192

SRVC_TYPE_SIGN_CHECK             = 0
SRVC_TYPE_IMG_DECRPT             = 1
SRVC_TYPE_FIRMWARE_DOWNLOAD      = 2
SRVC_TYPE_PUBKEY_DOWNLOAD        = 3
SRVC_TYPE_RSA_CRYPT_DECRYPT      = 4
SRVC_TYPE_ECDH_KEY               = 5
SRVC_TYPE_SYM_CRYPT_DECRYPT      = 6
SRVC_TYPE_DIGEST                 = 7
SRVC_TYPE_HMAC                   = 8
SRVC_TYPE_OTP_READ_PROGRAM       = 9
SRVC_TYPE_TRNG                   = 10
SRVC_TYPE_ADDR_REGION_PROTECTION = 11
SRVC_TYPE_DOWNLOADABLE           = 12
SRVC_TYPE_BASIC_IO               = 13
SRVC_TYPE_AXPROT                 = 14


class EswinIpcMiscDev:
    def __init__(self, device: str = '/dev/eswin_ipc_misc_dev0'):
        self.device = device
        self._fd = os.open(device, os.O_RDWR)
        self.nid = self._query_nid()

    def _query_nid(self) -> int:
        """Query NUMA node ID via SCPU_IOC_CPU_NID (cipher_get_nid_req_t)."""
        buf = bytearray(8)   # int cpuid + int nid
        fcntl.ioctl(self._fd, SCPU_IOC_CPU_NID, buf)
        _cpuid, nid = struct.unpack_from('<ii', buf)
        return nid

    def __enter__(self): return self

    def __exit__(self, *_): os.close(self._fd)

    def _request(self, srvc_type: int, req_data: bytes = b'') -> bytes:
        _HANDLE_REQ_SIZE = 2192   # sizeof(cipher_create_handle_req_t)
        _RESP_OFFSET     = 1056   # service_resp  (res_service_t)
        buf = bytearray(_HANDLE_REQ_SIZE)
        struct.pack_into('<II', buf, 0, 0, srvc_type)  # fill in service_req.service_type
        buf[8 : 8 + len(req_data)] = req_data  # fill in service_req.data
        fcntl.ioctl(self._fd, SCPU_IOC_MESSAGE_COMMUNICATION, buf)
        _num, _sid, ipc_status, service_status, _size = struct.unpack_from('<IIIII', buf, _RESP_OFFSET)
        assert (ipc_status == 0) and (service_status == 0)  # validate good response
        return bytes(buf[_RESP_OFFSET+20:])  # bytes starting after response header

    def read32(self, addr: int) -> int:
        _AXPROT_BASE  = 0x21B48100   # base address embedded in srvc_axprot in ROM
        _AXPROT_READ  = 0            # flag bits[1:0] = 0 → read operation
        assert (addr & 3)==0
        master_id = ((addr - _AXPROT_BASE) >> 2) & 0xFFFFFFFF  # master_id * 4 + 0x21B48100 == addr  (mod 2^32)
        req_data = struct.pack('<HxxII', _AXPROT_READ, master_id, 0)  # axprot_req_t: flag16_t(2) + pad(2) + master_id u32(4) + config u32(4)
        response = self._request(SRVC_TYPE_AXPROT, req_data)
        return struct.unpack_from('<I', response)[0]  # axprot_res_t.config is the first field of data_t, at offset RES_HDR_SIZE

    def trng_get_bytes(self, count: int) -> bytes:
        TRNG_DATA_MAX_LEN = 256
        assert (1 <= count <= TRNG_DATA_MAX_LEN)
        response = self._request(SRVC_TYPE_TRNG, struct.pack('<I', count))
        return response[:count]

    def read_bytes(self, addr: int, size: int) -> bytes:
        if size == 0: return b''
        start = addr & ~3  # align down to 4-byte boundaries
        end   = (addr + size + 3) & ~3  # align up to 4-byte boundaries
        raw = bytearray()
        for a in range(start, end, 4): raw += struct.pack('<I', self.read32(a))
        offset = addr - start
        return bytes(raw[offset : offset + size])  # return requested bytes from aligned data


def main():
    p = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                description='Reads data from SCPU via eswin-ipc-scpu and eswin-mailbox kernel drivers.',
                                epilog='''
examples:
  %(prog)s -t 256                            hex-dump 256 random bytes from TRNG vis SCPU
  %(prog)s -r 0x58000000                     hex-dump first 64 bytes of the masked ROM
  %(prog)s -r 0x30000000                     hex-dump first 64 bytes of the SRAM (TIM)
  %(prog)s -o rom.bin -r 0x58000000 0x20000  dump entire ROM to binary file rom.bin
  ''')
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument('-r', '--read',
                   type=lambda x: int(x, 0),
                   metavar='ADDR',
                   help='Read SCPU memory from given start address')
    g.add_argument('-t', '--trng',
                   action='store_true',
                   help='Read random bytes from the TRNG')
    p.add_argument('-d', '--device',
                   metavar='DEV',
                   default='/dev/eswin_ipc_misc_dev0',
                   help='IPC device (default %(default)s)')
    p.add_argument('-o', '--out',
                   metavar='FILE',
                   help='Write raw bytes to FILE instead of dumping hex to stdout')
    p.add_argument('count',
                   type=lambda x: int(x, 0),
                   nargs='?', default=0x40,
                   help='Number of bytes to read (default 64)')
    args = p.parse_args()

    with EswinIpcMiscDev(args.device) as ipc:
        data = b''
        if (args.trng):
            data = ipc.trng_get_bytes(args.count)
            args.read = 0  # base address for hex output
        elif (args.read is not None):
            data = ipc.read_bytes(args.read, args.count)

        if args.out:
            with open(args.out, 'wb') as f: f.write(data)
        else:
            for i in range(0, len(data), 16):
                chunk   = data[i:i+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                asc_str = ''.join(chr(b) if 0x20 <= b < 0x7f else '.' for b in chunk)
                print(f"{i+args.read:08x}:  {hex_str:<48}  {asc_str}")


if __name__ == '__main__':
    main()
