# Test-data generator for app/tests: writes small synthetic files in the
# real formats. No game data is involved.
import struct, sys
# mkxex.py out title media version base_version [target_version] [flags]
out=sys.argv[1]; title=int(sys.argv[2],16); media=int(sys.argv[3],16); ver=int(sys.argv[4],16); basev=int(sys.argv[5],16)
target = int(sys.argv[6],16) if len(sys.argv)>6 and sys.argv[6]!='-' else None
flags = int(sys.argv[7],16) if len(sys.argv)>7 else (0x41 if target is not None else 0x1)
headers=[(0x00010001, 0x82000000), (0x00040006, None)]  # a data-valued header + exec info
if target is not None: headers.insert(0,(0x000005FF,None))
n=len(headers)
data_off = 0x18 + n*8 + 0x20
blobs=b''; entries=b''
for k,v in headers:
    if v is None:
        off = data_off + len(blobs)
        if k==0x00040006:
            blobs += struct.pack('>IIII4BI', media, ver, basev, title, 0,0,1,1, 0)
        else:
            blobs += struct.pack('>III', 0x60, target, basev) + b'\0'*0x54
        v=off
    entries += struct.pack('>II', k, v)
header_size = data_off + len(blobs) + 0x100
hdr = struct.pack('>IIIIII', 0x58455832, flags, header_size, 0, 0, n)
body = hdr + entries + b'\0'*0x20 + blobs
body += b'\0'*(header_size-len(body)) + b'IMAGEDATA'*100
open(out,'wb').write(body)
