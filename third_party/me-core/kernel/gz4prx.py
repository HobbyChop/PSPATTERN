#!/usr/bin/env python3

"""
gz4prx.py

This script enables gzip compression of a kernel PRX file, generated
using the open sourced PSPSDK, and designed to be loaded from userland.

Thanks to:
Acid_Snake & krazynez for the original logic and guidance,
artart78 and the contributors of the UOFW, specialy regarding SceHeader and loadelf,
and isage for pointing me toward relevant information.

m-c/d
"""

import sys, os, io, struct, gzip, hashlib

gzip.time = type('_', (), {'time': lambda self: 0.0})()

def write_bytes(byte_array, offset, data):
  byte_array[offset:offset + len(data)] = data

def main():
  src, dst, mod_name = sys.argv[1:4]

  with open(src, 'rb') as f:
    elf = f.read(4)
    if elf != b'\x7fELF':
      print("Input file is not an ELF!")
      return -1
    
    f.seek(0);
    compressed = io.BytesIO()
    with gzip.GzipFile(fileobj=compressed, mode='wb', compresslevel=9) as g:
      g.writelines(f)
    
    data = compressed.getvalue()
    compressed.close()

  header_size = 0x150;
  src_size = os.stat(src).st_size
  dst_size = header_size + len(data)
  mod_name = mod_name.encode('utf-8')[:28]
  md5_hash = hashlib.md5(data).digest()
  
  header = bytearray(header_size);
  write_bytes(header, 0x000, struct.pack('I', 0x5053507e))
  write_bytes(header, 0x004, struct.pack('H', 0x3007))
  write_bytes(header, 0x006, struct.pack('H', 0x0001))
  write_bytes(header, 0x008, struct.pack('B', 0x05))
  write_bytes(header, 0x009, struct.pack('B', 0x02))
  write_bytes(header, 0x00a, mod_name)
  write_bytes(header, 0x028, struct.pack('L', src_size))
  write_bytes(header, 0x02c, struct.pack('L', dst_size))
  write_bytes(header, 0x034, struct.pack('I', 0x80000000))
  write_bytes(header, 0x038, struct.pack('I', 0x00000090))
  write_bytes(header, 0x078, struct.pack('I', 0x00000003))
  write_bytes(header, 0x07c, struct.pack('B', 0x01))
  write_bytes(header, 0x0b0, struct.pack('L', len(data)))
  write_bytes(header, 0x130, struct.pack('I', 0xc01db15d))
  write_bytes(header, 0x140, md5_hash)
  
  f = open(dst, "wb")
  f.write(header)
  f.write(data)
  f.close()
  
  return 0

if __name__ == "__main__":
  sys.exit(main())
