#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "construct",
# ]
# ///
"""
Extracts sub-images from eswin firmware files (ESWB magic).
"""

from construct import (
    Struct, Int8ul, Int32ul, Int64ul,
    Bytes, Array, Enum
)

import sys
from pathlib import Path

# Constants
BTL_HASH_DIG_SIZE = 32
SIGNATURE_SIZE = 256

PayloadType = Enum(Int8ul,
    PUBKEY_RSA      = 0x00,
    PUBKEY_ECC      = 0x01,
    DDR             = 0x10,
    D2D             = 0x20,
    BOOTLOADER      = 0x30,
    KERNEL          = 0x40,
    ROOTFS          = 0x50,
    APPLICATION     = 0x60,
    FIRMWARE        = 0x70,
    PATCH           = 0x80,
    LOADABLE_SRVC   = 0x90,
)

FirmwareEntryHeader = Struct(
    "version"        / Int32ul,
    "offset"         / Int64ul,
    "size"           / Int64ul,
    "sign_type"      / Int8ul,
    "key_index"      / Int8ul,
    "payload_type"   / PayloadType,
    "last_flag"      / Int8ul,
    "reserved0"      / Bytes(4),
    "reserved1"      / Int32ul,
    "reserved2"      / Int32ul,
)

FirmwareHeader = Struct(
    "magic"        / Int32ul,
    "num_entries"  / Int32ul,
    "entries"      / Array(lambda ctx: ctx.num_entries, FirmwareEntryHeader)
)

LoadableInfo = Struct(
    "load_addr"    / Int32ul,
    "init_ofs"     / Int32ul,
    "destroy_ofs"  / Int32ul,
    "ioctl_ofs"    / Int32ul,
    "load_flags"   / Int32ul,
    "irq_num"      / Int32ul,
    "irq_ofs"      / Int32ul,
)

Signature = Struct(
    "magic" / Int32ul,
    "reserved0" / Int32ul,
    "link_addr" / Int64ul , # in use for loadable service
    "payload_offset" / Int64ul,
    "payload_size" / Int64ul,
    "load_addr" / Int64ul,
    "entry_addr" / Int64ul,  # Entry address of the program and CPU will jump into
    "payload_flags" / Int8ul,  # Payload is encrypted or not */
    "digest_mthd" / Int8ul,  # digest algorithm use SHA256 or SM3
    "encrypted_mthd" / Int8ul,
    "vid" / Int8ul,  # vendor id
    "reserved1" / Int8ul,
    "lang" / Bytes(3),
    "mid" / Int64ul,  # market ID
    "payload_type" / PayloadType,
    "boot_flags" / Int8ul, # Boot by SCPU or MCPU (= 1)
    "reserved2" / Bytes(6),
    "devid" / Int64ul, # device ID
    "params" / Bytes(16),  # Parameters for next boot stage
    "reserved3" / Bytes(16),
    "load_info" / LoadableInfo,
    "reserved4" / Int32ul,
    "digest" / Bytes(32),
)

def main():
    path=Path(sys.argv[1])
    with open(path, "rb") as f:
        data = f.read()
        header = FirmwareHeader.parse(data)
        #print(header)
        print(f"header.magic=0x{header.magic:08X}")
        print(f"{header.num_entries = }")
        for h in header.entries:
            part_filename = f'{path.parent}/{path.stem}_{h.payload_type}{path.suffix}'
            print(f"{part_filename}")
            print(f"  {h.offset = } 0x{h.offset:x}")
            print(f"  {h.size = } 0x{h.size:x} end: 0x{h.offset+h.size:x}")
            print(f"  {h.sign_type = } {h.key_index = }")
            print(f"  h.payload_type = {h.payload_type} {h.last_flag = }")
            s = Signature.parse(data[h.offset:h.offset+256])
            print(f"  s.link_addr = 0x{s.link_addr:x}")
            print(f"  {s.payload_offset = } 0x{s.payload_offset:x} (ignored)")
            print(f"  {s.payload_size = } 0x{s.payload_size:x} end: 0x{s.payload_offset+s.payload_size:x}")
            print(f"  s.load_addr = 0x{s.load_addr:x}")
            print(f"  s.entry_addr = 0x{s.entry_addr:x}")
            print(f"  {s.payload_flags = }")
            print(f"  {s.digest_mthd = }")
            print(f"  {s.encrypted_mthd = }")
            print(f"  {s.vid = }")
            print(f"  {s.lang = }")
            print(f"  {s.mid = }")
            print(f"  s.payload_type = {s.payload_type}")
            print(f"  {s.boot_flags = }")
            print(f"  {s.devid = }")
            print(f"  {s.params = }")
            print(f"  {s.load_info = }")
            print(f"  {s.digest = }")
            with open(part_filename, "wb") as pf:
                pf.write(data[h.offset+256:h.offset+h.size+256])

if __name__ == "__main__":
    main()
