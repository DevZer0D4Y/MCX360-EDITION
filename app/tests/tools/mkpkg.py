# Test-data generator for app/tests: writes small synthetic files in the
# real formats. No game data is involved.
# mkpkg.py out magic content_type_hex title_hex version_hex "Display Name"
import struct, sys
out,magic,ct,title,ver,name=sys.argv[1],sys.argv[2],int(sys.argv[3],16),int(sys.argv[4],16),int(sys.argv[5],16),sys.argv[6]
h=bytearray(0xA000)
h[0:4]=magic.encode().ljust(4)
struct.pack_into('>I',h,0x344,ct); struct.pack_into('>I',h,0x354,0x7CD33B56); struct.pack_into('>I',h,0x358,ver); struct.pack_into('>I',h,0x360,title)
nm=name.encode('utf-16-be')[:0x80]; h[0x411:0x411+len(nm)]=nm
open(out,'wb').write(bytes(h))
