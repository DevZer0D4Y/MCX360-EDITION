# Test-data generator for app/tests: writes small synthetic files in the
# real formats. No game data is involved.
# mkstfs.py out.pkg title_hex version_hex name=src ...   (read-only STFS, LIVE)
# Block placement mirrors StfsContainerDevice::BlockToOffsetSTFS /
# BlockToHashBlockNumberSTFS (blocks_per_hash_table = 1 for read-only format).
import struct, sys, random
out=sys.argv[1]; title=int(sys.argv[2],16); version=int(sys.argv[3],16)
files=[]
for a in sys.argv[4:]:
    k,v=a.split('=',1); files.append((k, open(v,'rb').read()))
BS=0x1000; L0=170; L1=28900; BPHT=1
step0=L0+BPHT
def phys(idx):
    base=L0; block=idx
    for i in range(3):
        block += ((idx+base)//base)*BPHT
        if idx < base: break
        base *= L0
    return block
def hashblock(idx):
    if idx < L0: return 0
    b=(idx//L0)*step0 + ((idx//L1)+1)*BPHT
    return b if idx < L1 else b+BPHT
# directories
dirs=[]; entries=[]   # entries: (name, parent_index, is_dir, data)
index={}
def dir_index(path):
    if path=='' : return 0xFFFF
    if path in index: return index[path]
    parent,_,name=path.rpartition('/')
    pi=dir_index(parent)
    entries.append((name,pi,True,b'')); index[path]=len(entries)-1; return index[path]
for p,data in files:
    parent,_,name=p.rpartition('/')
    pi=dir_index(parent)
    entries.append((name,pi,False,data))
# allocate blocks: block 0 = file table; data blocks scattered
nblocks=[(len(d)+BS-1)//BS for (_,_,isd,d) in entries]
total=1+sum(n for n,(_,_,isd,_) in zip(nblocks,entries) if not isd)
free=list(range(1,total)); random.seed(7)
# keep the first file contiguous, shuffle the rest so chains jump around
chains=[]
for n,(name,pi,isd,d) in zip(nblocks,entries):
    if isd: chains.append([]); continue
    if len(chains)==0 or len(free) < n: pass
    take=free[:n]; free=free[n:]
    if n>1 and len(chains)>1: random.shuffle(take)
    chains.append(take)
nexts={0:0xFFFFFF}
for ch in chains:
    for i,b in enumerate(ch): nexts[b]= ch[i+1] if i+1<len(ch) else 0xFFFFFF
header_size=0x971A
data_start=(header_size+BS-1)//BS*BS
img=bytearray(data_start + (phys(total-1)+1)*BS)
# header
img[0:4]=b'LIVE'
struct.pack_into('>I',img,0x340,header_size)
struct.pack_into('>I',img,0x344,0x000B0000)          # content type: installer (title update)
struct.pack_into('>IIII',img,0x354,0x7CD33B56,version,0,title)
vd=0x379
img[vd]=0x24; img[vd+1]=0; img[vd+2]=0x01             # read-only format
struct.pack_into('<H',img,vd+3,1)                     # file table block count
img[vd+5:vd+8]=(0).to_bytes(3,'little')               # file table block number
struct.pack_into('>II',img,vd+0x1C,total,0)
struct.pack_into('>I',img,0x3A9,0)                    # volume type: STFS
nm='Minecraft'.encode('utf-16-be'); img[0x411:0x411+len(nm)]=nm
# file table (block 0)
ft=bytearray(BS)
for i,((name,pi,isd,d),ch) in enumerate(zip(entries,chains)):
    e=bytearray(0x40); e[0:len(name)]=name.encode()
    e[0x28]=len(name) | (0x80 if isd else 0)
    nb=len(ch)
    e[0x29:0x2C]=nb.to_bytes(3,'little'); e[0x2C:0x2F]=nb.to_bytes(3,'little')
    e[0x2F:0x32]=(ch[0] if ch else 0).to_bytes(3,'little')
    struct.pack_into('>H',e,0x32,pi); struct.pack_into('>I',e,0x34,len(d))
    ft[i*0x40:(i+1)*0x40]=e
off=data_start+phys(0)*BS; img[off:off+BS]=ft
for (name,pi,isd,d),ch in zip(entries,chains):
    for i,b in enumerate(ch):
        chunk=d[i*BS:(i+1)*BS]; o=data_start+phys(b)*BS; img[o:o+len(chunk)]=chunk
# level-0 hash tables: next-block links
for b,n in nexts.items():
    t=data_start+hashblock(b)*BS + (b % L0)*0x18
    struct.pack_into('>I',img,t+0x14,(0x80<<24)|n)
open(out,'wb').write(bytes(img))
print(f"{out}: {total} blocks, {len(img)} bytes")
