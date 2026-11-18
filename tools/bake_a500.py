# Bakes the Commodore Amiga 500 model for the native player.
# usage: python3 tools/bake_a500.py A500.glb > src/a500_data.h   (needs numpy and Pillow)
# The GLB has one colour per material and one textured part (the top of the case with the keyboard); the data has the same layout as the
# sound cards of card3d_data.h (see tools/bake_card3d.py), minus the printed circuit board on the back. The model is kept upright (y up,
# keys towards +z) and centred, one unit is half the width of the computer.
import os,sys,io,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from glb import load,acc
from PIL import Image
Q=20000.0
TEXW=384
TAG='A500'
def arr(name,ctype,vals,per=16):
    s='static const %s %s[%d] = {\n'%(ctype,name,len(vals))
    line=[]
    for v in vals:
        line.append(str(int(v)))
        if len(line)==per: s+=','.join(line)+',\n';line=[]
    if line: s+=','.join(line)+',\n'
    return s+'};\n'

j,b=load(sys.argv[1])
V=[];VC=[];I=[];pal=[];TV=[];TUV=[];TI=[]
for n in j['nodes']:
    m=j['meshes'][n['mesh']];pr=m['primitives'][0];at=pr['attributes']
    p_,_=acc(j,b,at['POSITION']);ia,_=acc(j,b,pr['indices']);p_=p_.astype(np.float64)
    mat=j['materials'][pr['material']]['pbrMetallicRoughness']
    if 'TEXCOORD_0' in at:
        uv,_=acc(j,b,at['TEXCOORD_0']);base=len(TV)
        for k in range(len(p_)): TV.append(p_[k]);TUV.append(uv[k])
        TI+=[base+int(i) for i in ia]
    else:
        col=tuple(int(round(x*255)) for x in mat['baseColorFactor'][:3])
        if col not in pal: pal.append(col)
        base=len(V)
        for k in range(len(p_)): V.append(p_[k]);VC.append(pal.index(col))
        I+=[base+int(i) for i in ia]
V=np.array(V);TV=np.array(TV);TUV=np.array(TUV,dtype=np.float64)
allp=np.vstack([V,TV]);lo=allp.min(0);hi=allp.max(0)
c=(lo+hi)/2;half=(hi[0]-lo[0])/2
def quant(P): return np.round((P-c)/half*Q).astype(int).flatten()
img_bv=j['bufferViews'][j['images'][0]['bufferView']]
img=Image.open(io.BytesIO(b[img_bv.get('byteOffset',0):img_bv.get('byteOffset',0)+img_bv['byteLength']])).convert('RGB')
th=int(round(TEXW*img.height/img.width));img=img.resize((TEXW,th),Image.LANCZOS)
a=np.asarray(img).astype(np.uint16);t565=((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)
w,h,d=hi-lo
print('A500: verts',len(V),'tris',len(I)//3,'palette',len(pal),'texverts',len(TV),'tex',TEXW,th,'size',w,h,d,file=sys.stderr)
s='/* Commodore Amiga 500, baked from the 3D model by tools/bake_a500.py (same layout as the cards of card3d_data.h, no back side) */\n#include <stdint.h>\n'
s+='#define C3_%s_NV %d\n#define C3_%s_NT %d\n#define C3_%s_NTV %d\n#define C3_%s_NTT %d\n#define C3_%s_TW %d\n#define C3_%s_TH %d\n'%(TAG,len(V),TAG,len(I)//3,TAG,len(TV),TAG,len(TI)//3,TAG,TEXW,TAG,th)
s+='#define C3_%s_ASPECT %.4ff            /* width / depth; the box of the turning model is set where it is drawn */\n'%(TAG,w/d)
s+='#define C3_%s_HEIGHT %.4ff            /* height of the model in units of half the width */\n#define C3_%s_DEPTH %.4ff             /* depth of the model in units of half the width */\n'%(TAG,h/half,TAG,d/half)
s+=arr('c3_%s_v'%TAG,'int16_t',quant(V))+arr('c3_%s_vc'%TAG,'uint8_t',VC,32)+arr('c3_%s_i'%TAG,'uint16_t',I)
s+=arr('c3_%s_pal'%TAG,'uint8_t',[x for col in pal for x in col])
s+=arr('c3_%s_tv'%TAG,'int16_t',quant(TV))+arr('c3_%s_tuv'%TAG,'uint16_t',np.round(TUV.flatten()*65535).astype(int))+arr('c3_%s_ti'%TAG,'uint16_t',TI)
s+=arr('c3_%s_tex'%TAG,'uint16_t',t565.flatten(),12)
s+='/* no back side: an empty mesh and a 1 x 1 picture */\n#define C3_%s_BW 1\n#define C3_%s_BH 1\n#define C3_%s_NBV 0\n#define C3_%s_NBT 0\n'%(TAG,TAG,TAG,TAG)
s+=arr('c3_%s_bv'%TAG,'int16_t',[0,0,0])+arr('c3_%s_buv'%TAG,'uint16_t',[0,0])+arr('c3_%s_bi'%TAG,'uint16_t',[0,0,0])+arr('c3_%s_bpal'%TAG,'uint8_t',[0,0,0])+arr('c3_%s_bpix'%TAG,'uint8_t',[0])
sys.stdout.write(s)
