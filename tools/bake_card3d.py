# usage: python3 tools/bake_card3d.py DIR_WITH_GLB_FILES > src/card3d_data.h   (needs numpy and Pillow)
# DIR must hold gus_ultrasound_rev34.glb (GF1) and gus_pnp_v30.glb (InterWave); output goes to the file given as second argument
import os,sys,io,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from glb import *
from PIL import Image
U=sys.argv[1].rstrip('/')+'/'
Q=20000.0
def arr(name,ctype,vals,per=16):
    s='static const %s %s[%d] = {\n'%(ctype,name,len(vals))
    line=[]
    for i,v in enumerate(vals):
        line.append(str(int(v)))
        if len(line)==per: s+=','.join(line)+',\n';line=[]
    if line: s+=','.join(line)+',\n'
    return s+'};\n'
def bake(tag,glb,texw):
    j,b=load(U+glb)
    M=np.array(j['nodes'][0]['matrix']).reshape(4,4).T
    V=[];VC=[];I=[];pal=[];TV=[];TUV=[];TI=[]
    for ni,n in enumerate(j['nodes']):
        m=j['meshes'][n['mesh']];pr=m['primitives'][0];at=pr['attributes']
        p_,_=acc(j,b,at['POSITION']);ia,_=acc(j,b,pr['indices'])
        p_=p_.astype(np.float64)
        if 'TEXCOORD_0' in at:
            uv,_=acc(j,b,at['TEXCOORD_0']);base=len(TV)
            for k in range(len(p_)): TV.append(p_[k]);TUV.append(uv[k])
            TI+= [base+int(i) for i in ia]
        else:
            c,_=acc(j,b,at['COLOR_0']);col=tuple(int(x) for x in c[0][:3])
            if col not in pal: pal.append(col)
            base=len(V)
            for k in range(len(p_)): V.append(p_[k]);VC.append(pal.index(col))
            I+=[base+int(i) for i in ia]
    V=np.array(V);TV=np.array(TV);TUV=np.array(TUV,dtype=np.float64)
    allp=np.vstack([V,TV]);lo=allp.min(0);hi=allp.max(0)
    cx,cy=(lo[0]+hi[0])/2,(lo[1]+hi[1])/2;cz=0.8
    half=(hi[0]-lo[0])/2
    def quant(P): return np.round(np.column_stack([(P[:,0]-cx)/half,(P[:,1]-cy)/half,(P[:,2]-cz)/half])*Q).astype(int).flatten()
    img_bv=j['bufferViews'][j['images'][0]['bufferView']];img=Image.open(io.BytesIO(b[img_bv.get('byteOffset',0):img_bv.get('byteOffset',0)+img_bv['byteLength']])).convert('RGB');th=int(round(texw*img.height/img.width));img=img.resize((texw,th),Image.LANCZOS)
    a=np.asarray(img).astype(np.uint16);t565=((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)
    print(tag,'verts',len(V),'tris',len(I)//3,'palette',len(pal),'texverts',len(TV),'tex',texw,th,'half',half,'zmax',hi[2],'aspect',(hi[0]-lo[0])/(hi[1]-lo[1]))
    s='/* %s: baked from the 3D model, see tools/bake_card3d.py */\n#define C3_%s_NV %d\n#define C3_%s_NT %d\n#define C3_%s_NTV %d\n#define C3_%s_NTT %d\n#define C3_%s_TW %d\n#define C3_%s_TH %d\n#define C3_%s_ASPECT %.4ff\n'%(tag,tag,len(V),tag,len(I)//3,tag,len(TV),tag,len(TI)//3,tag,texw,tag,th,tag,(hi[0]-lo[0])/(hi[1]-lo[1]))
    s+=arr('c3_%s_v'%tag,'int16_t',quant(V))+arr('c3_%s_vc'%tag,'uint8_t',VC,32)+arr('c3_%s_i'%tag,'uint16_t',I)
    s+=arr('c3_%s_pal'%tag,'uint8_t',[x for c in pal for x in c])
    s+=arr('c3_%s_tv'%tag,'int16_t',quant(TV))+arr('c3_%s_tuv'%tag,'uint16_t',np.round(TUV.flatten()*65535).astype(int))+arr('c3_%s_ti'%tag,'uint16_t',TI)
    s+=arr('c3_%s_tex'%tag,'uint16_t',t565.flatten(),12)
    return s
out='/* 3D models of the Gravis Ultrasound (GF1, rev 3.4) and Ultrasound PnP (AMD InterWave) cards. GPL-3.0-or-later */\n#include <stdint.h>\n'
out+=bake('GF1','gus_ultrasound_rev34.glb',320)+bake('IW','gus_pnp_v30.glb',320)
open(sys.argv[2] if len(sys.argv)>2 else 'src/card3d_data.h','w').write(out);print(len(out)//1024,'KB')
