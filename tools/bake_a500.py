# Bakes the Commodore Amiga 500 model for the player.
# usage: python3 tools/bake_a500.py assets/models/A500-sketchup.zip > src/a500_data.h   (needs numpy, Pillow and the assimp command line tool)
# The zip is a COLLADA export of a SketchUp model: model.dae and a folder model/ with the textures. assimp turns it into a GLB first (the edges
# SketchUp draws as lines are dropped). The data has the layout of the sound cards of card3d_data.h (see tools/bake_card3d.py), minus the back side:
#  - every part with a plain material gets a palette colour; parts with a small or repeating texture (the keys, the grille) get the average
#    colour of their texture, which is all one can see at the size of the card;
#  - the four big textures (top, front and sides, rear, the strip below the keys) are packed into one atlas.
# The model is turned upright (the SketchUp model lies with z up and its front at -y) and centred; one unit is half the width of the computer.
import os,sys,io,json,zipfile,tempfile,subprocess,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from glb import load,acc
from PIL import Image
Q=20000.0
ATLASW=768
BIG=100000                 # textures with more pixels than this go into the atlas
TAG='A500'
def arr(name,ctype,vals,per=16):
    s='static const %s %s[%d] = {\n'%(ctype,name,len(vals))
    line=[]
    for v in vals:
        line.append(str(int(v)))
        if len(line)==per: s+=','.join(line)+',\n';line=[]
    if line: s+=','.join(line)+',\n'
    return s+'};\n'

tmp=tempfile.mkdtemp(prefix='a500bake')
zipfile.ZipFile(sys.argv[1]).extractall(tmp)
subprocess.run(['assimp','export',os.path.join(tmp,'model.dae'),os.path.join(tmp,'model.glb')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
j,b=load(os.path.join(tmp,'model.glb'))
mats=j['materials']
def image_of(mat):
    pbr=mat.get('pbrMetallicRoughness',{})
    if 'baseColorTexture' not in pbr: return None
    return j['images'][j['textures'][pbr['baseColorTexture']['index']]['source']]['uri']
cache={}
def img(uri):
    if uri not in cache: cache[uri]=Image.open(os.path.join(tmp,uri)).convert('RGB')
    return cache[uri]

# ---- collect the triangle parts
parts=[]
for m in j['meshes']:
    for pr in m['primitives']:
        if pr.get('mode',4)!=4: continue
        at=pr['attributes'];p,_=acc(j,b,at['POSITION']);ia,_=acc(j,b,pr['indices'])
        mat=mats[pr['material']];uri=image_of(mat)
        uv=acc(j,b,at['TEXCOORD_0'])[0].astype(np.float64) if (uri and 'TEXCOORD_0' in at) else None
        parts.append(dict(p=p.astype(np.float64),i=ia.astype(np.int64).flatten(),mat=mat,uri=uri,uv=uv))
# the SketchUp model: x right, z up, front at -y  ->  x right, y up, front at +z
def turn(p): return np.column_stack([p[:,0],p[:,2],-p[:,1]])
for q in parts: q['p']=turn(q['p'])
big=sorted({q['uri'] for q in parts if q['uri'] and img(q['uri']).size[0]*img(q['uri']).size[1]>BIG})
# atlas rows
rows={};y=0
for u in big:
    w,h=img(u).size;th=max(8,int(round(ATLASW*h/w)));rows[u]=(y,th);y+=th+3
ATLASH=(y+7)//8*8
atlas=Image.new('RGB',(ATLASW,ATLASH),(128,128,128))
for u in big:
    y0,th=rows[u];atlas.paste(img(u).resize((ATLASW,th),Image.LANCZOS),(0,y0))
    for g in range(1,3): atlas.paste(atlas.crop((0,y0+th-1,ATLASW,y0+th)),(0,y0+th-1+g))      # a gutter against bleeding

V=[];VC=[];I=[];TV=[];TUV=[];TI=[];cols=[]
def colour_of(q):
    if q['uri'] is None:
        f=q['mat'].get('pbrMetallicRoughness',{}).get('baseColorFactor',[0.7,0.7,0.7,1]);c=tuple(int(round(x*255)) for x in f[:3])
        if max(abs(c[0]-139),abs(c[1]-136),abs(c[2]-154))<=2: c=(190,175,146)          # the violet grey of the model's material "material" (7371 triangles at the rear and the vents): the case is beige
        return c
    im=img(q['uri'])
    if q['uv'] is not None and q['uv'].min()>=-0.001 and q['uv'].max()<=1.001:
        u0,v0=q['uv'].min(0);u1,v1=q['uv'].max(0);w,h=im.size
        box=(int(u0*w),int(v0*h),max(int(u0*w)+1,int(np.ceil(u1*w))),max(int(v0*h)+1,int(np.ceil(v1*h))));im=im.crop(box)
    a=np.asarray(im).reshape(-1,3).mean(0);return tuple(int(round(x)) for x in a)
for q in parts:
    if q['uri'] in rows:
        y0,th=rows[q['uri']];uv=np.clip(q['uv'],0.0,1.0)
        base=len(TV)
        for k in range(len(q['p'])): TV.append(q['p'][k]);TUV.append(((uv[k][0]*(ATLASW-1)+0.5)/ATLASW,(y0+uv[k][1]*(th-1)+0.5)/ATLASH))
        TI+=[base+int(t) for t in q['i']]
    else:
        c=colour_of(q);base=len(V);cols.append(c);ci=len(cols)-1
        for k in range(len(q['p'])): V.append(q['p'][k]);VC.append(ci)
        I+=[base+int(t) for t in q['i']]
# palette: at most 255 colours
uniq=sorted(set(cols))
if len(uniq)>255:
    pim=Image.new('RGB',(len(uniq),1));pim.putdata(uniq);qi=pim.quantize(255,method=Image.Quantize.MEDIANCUT)
    pal_flat=qi.getpalette()[:3*255];remap=list(qi.getdata());pal=[tuple(pal_flat[3*k:3*k+3]) for k in range(255)]
    m={c:remap[k] for k,c in enumerate(uniq)}
else:
    pal=uniq;m={c:k for k,c in enumerate(uniq)}
VC=[m[cols[ci]] for ci in VC]
V=np.array(V);TV=np.array(TV);TUV=np.array(TUV)
allp=np.vstack([V,TV]);lo=allp.min(0);hi=allp.max(0);c=(lo+hi)/2;half=(hi[0]-lo[0])/2
def quant(P): return np.round((P-c)/half*Q).astype(int).flatten()
a=np.asarray(atlas).astype(np.uint16);t565=((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)
w,h,d=hi-lo
print('A500: verts',len(V),'tris',len(I)//3,'palette',len(pal),'texverts',len(TV),'textris',len(TI)//3,'atlas',ATLASW,ATLASH,'size',w.round(1),h.round(1),d.round(1),file=sys.stderr)
assert len(V)<65536 and len(TV)<65536
s='/* Commodore Amiga 500, baked from a 3D model by tools/bake_a500.py (same layout as the cards of card3d_data.h, no back side) */\n#include <stdint.h>\n'
s+='#define C3_%s_NV %d\n#define C3_%s_NT %d\n#define C3_%s_NTV %d\n#define C3_%s_NTT %d\n#define C3_%s_TW %d\n#define C3_%s_TH %d\n'%(TAG,len(V),TAG,len(I)//3,TAG,len(TV),TAG,len(TI)//3,TAG,ATLASW,TAG,ATLASH)
s+='#define C3_%s_ASPECT %.4ff            /* width / depth; the box of the turning model is set where it is drawn */\n'%(TAG,w/d)
s+='#define C3_%s_HEIGHT %.4ff            /* height of the model in units of half the width */\n#define C3_%s_DEPTH %.4ff             /* depth of the model in units of half the width */\n'%(TAG,h/half,TAG,d/half)
s+=arr('c3_%s_v'%TAG,'int16_t',quant(V))+arr('c3_%s_vc'%TAG,'uint8_t',VC,32)+arr('c3_%s_i'%TAG,'uint16_t',I)
s+=arr('c3_%s_pal'%TAG,'uint8_t',[x for col in pal for x in col])
s+=arr('c3_%s_tv'%TAG,'int16_t',quant(TV))+arr('c3_%s_tuv'%TAG,'uint16_t',np.round(TUV.flatten()*65535).astype(int))+arr('c3_%s_ti'%TAG,'uint16_t',TI)
s+=arr('c3_%s_tex'%TAG,'uint16_t',t565.flatten(),12)
s+='/* no back side: an empty mesh and a 1 x 1 picture */\n#define C3_%s_BW 1\n#define C3_%s_BH 1\n#define C3_%s_NBV 0\n#define C3_%s_NBT 0\n'%(TAG,TAG,TAG,TAG)
s+=arr('c3_%s_bv'%TAG,'int16_t',[0,0,0])+arr('c3_%s_buv'%TAG,'uint16_t',[0,0])+arr('c3_%s_bi'%TAG,'uint16_t',[0,0,0])+arr('c3_%s_bpal'%TAG,'uint8_t',[0,0,0])+arr('c3_%s_bpix'%TAG,'uint8_t',[0])
sys.stdout.write(s)
if os.environ.get('ATLAS_PREVIEW'): atlas.save(os.environ['ATLAS_PREVIEW'])
