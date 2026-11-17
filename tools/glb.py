import struct,json,numpy as np,sys
def load(path):
    d=open(path,'rb').read()
    l,t=struct.unpack('<II',d[12:20]);j=json.loads(d[20:20+l]);off=20+l
    cl,ct=struct.unpack('<II',d[off:off+8]);bin_=d[off+8:off+8+cl]
    return j,bin_
def acc(j,b,i):
    a=j['accessors'][i];bv=j['bufferViews'][a['bufferView']]
    ct={5120:'i1',5121:'u1',5122:'i2',5123:'u2',5125:'u4',5126:'f4'}[a['componentType']]
    n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    o=bv.get('byteOffset',0)+a.get('byteOffset',0)
    arr=np.frombuffer(b,dtype=np.dtype(ct),count=a['count']*n,offset=o) if 'byteStride' not in bv else None
    return arr.reshape(-1,n) if n>1 else arr, a
if __name__=='__main__':
    for p in sys.argv[1:]:
        j,b=load(p);tris=0;verts=0;kinds={}
        for m in j['meshes']:
            for pr in m['primitives']:
                at=pr['attributes'];p_,a=acc(j,b,at['POSITION']);verts+=len(p_)
                if 'indices' in pr: ia,_=acc(j,b,pr['indices']);tris+=len(ia)//3
                else: tris+=len(p_)//3
                k=('tex' if 'TEXCOORD_0' in at else 'col');kinds[k]=kinds.get(k,0)+1
                if pr.get('mode',4)!=4: print('mode',pr.get('mode'))
        print(p.split('/')[-1],'tris',tris,'verts',verts,kinds)
        img=j['images'][0];bv=j['bufferViews'][img['bufferView']];open(p.split('/')[-1]+'.png','wb').write(b[bv.get('byteOffset',0):bv.get('byteOffset',0)+bv['byteLength']])
        if 'COLOR_0' in j['meshes'][0]['primitives'][0]['attributes']:
            c,a=acc(j,b,j['meshes'][0]['primitives'][0]['attributes']['COLOR_0']);print('color acc',a['componentType'],a['type'],a.get('normalized'),c[:2])
