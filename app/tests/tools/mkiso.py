# Test-data generator for app/tests: writes small synthetic files in the
# real formats. No game data is involved.
# mkiso.py out.iso game_offset_hex file1=src1 file2=src2 ...  (paths with '/' make dirs)
# Writes an XDVDFS image. Directory tables use both left and right links.
import struct, sys, os
out=sys.argv[1]; game_off=int(sys.argv[2],16)
files={}
for a in sys.argv[3:]:
    k,v=a.split('=',1); files[k]=open(v,'rb').read() if v!='@empty' else b''
SECT=0x800
# build tree
def tree():
    root={}
    for p,data in files.items():
        parts=p.split('/'); d=root
        for x in parts[:-1]: d=d.setdefault(x,{})
        d[parts[-1]]=data
    return root
next_sector=[34]  # after volume descriptor (sector 32)
data_blobs=[]  # (sector, bytes)
def alloc(n):
    s=next_sector[0]; next_sector[0]+= max(1,(n+SECT-1)//SECT); return s
def build_dir(d):
    names=sorted(d.keys())
    # entries: first entry links others: left -> 2nd, right chain for rest
    recs=[]
    for name in names:
        v=d[name]
        if isinstance(v,dict):
            tbl=build_dir(v); sec=alloc(len(tbl)); data_blobs.append((sec,tbl)); recs.append((name,sec,len(tbl),0x10))
        else:
            sec=alloc(len(v)); data_blobs.append((sec,v)); recs.append((name,sec,len(v),0x80))
    # layout offsets
    offs=[]; o=0
    for name,_,_,_ in recs:
        offs.append(o); l=14+len(name); o+= (l+3)//4*4
    tbl=bytearray(b'\xff'* max(o, 16))
    n=len(recs)
    for i,(name,sec,size,attr) in enumerate(recs):
        left = offs[1]//4 if (i==0 and n>1) else 0
        if i==0: right = offs[2]//4 if n>2 else 0
        elif i==1: right=0
        else: right = offs[i+1]//4 if i+1<n else 0
        rec=struct.pack('<HHIIBB',left,right,sec,size,attr,len(name))+name.encode()
        tbl[offs[i]:offs[i]+len(rec)]=rec
        pad=(len(rec)+3)//4*4-len(rec); tbl[offs[i]+len(rec):offs[i]+len(rec)+pad]=b'\0'*pad
    return bytes(tbl)
root=build_dir(tree())
rsec=alloc(len(root)); data_blobs.append((rsec,root))
with open(out,'wb') as f:
    vd=b'MICROSOFT*XBOX*MEDIA'+struct.pack('<II',rsec,len(root))
    f.seek(game_off+32*SECT); f.write(vd)
    for sec,b in data_blobs:
        f.seek(game_off+sec*SECT); f.write(b)
    end=game_off+next_sector[0]*SECT
    f.truncate(end)
