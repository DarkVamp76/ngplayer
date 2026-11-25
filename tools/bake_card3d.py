# usage: python3 tools/bake_card3d.py DIR_WITH_GLB_FILES > src/card3d_data.h   (needs numpy and Pillow)
# DIR must hold gus_ultrasound_rev34.glb (GF1), gus_pnp_v30.glb (InterWave), ews64.glb and adlib_gold.glb; output goes to the file given as second argument
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

PAL=[(112,12,14),(88,8,11),(150,32,28),(205,205,198),(236,236,230),(216,172,62),(28,18,18),(172,152,112),(162,162,156),(126,16,18),(60,6,8),(185,60,50),(140,140,135),(238,200,110),(100,10,12),(70,5,8)]
def pack4(img):
    f=img.flatten();
    if len(f)%2: f=np.append(f,0)
    return (f[0::2]<<4)|f[1::2]
def make_back(tag,PRIMS,QUADS):
    global BX0,BX1,BY0,BY1
    from PIL import ImageDraw,ImageFont
    import random
    random.seed(7)
    core=PRIMS[0];BX0,BX1=core[0][0],core[1][0];BY1=core[1][1];BY0=0.0
    W=480;sc=W/(BX1-BX0);H=int(round((BY1-BY0)*sc));S=3
    im=Image.new('RGB',(W*S,H*S),PAL[0]);d=ImageDraw.Draw(im)
    def px(x,y): return ((BX1-x)*sc*S,(BY1-y)*sc*S)           # seen from behind: x is mirrored
    comps=[]
    for lo,hi,col in PRIMS[1:]:
        if hi[2]<=1.7 or lo[0]<BX0-1 or hi[0]>BX1+1 or lo[1]<7.9 or hi[1]>BY1+0.5: continue
        w=hi[0]-lo[0];h=hi[1]-lo[1]
        if w>90 or h>90: continue
        comps.append((lo,hi))
    # traces: every part to its two nearest neighbours, Manhattan routes
    cen=[((l[0]+h[0])/2,(l[1]+h[1])/2) for l,h in comps]
    tw=max(1.0,0.5*sc)*S
    for i,(cx,cy) in enumerate(cen):
        ds=sorted(range(len(cen)),key=lambda j:(cen[j][0]-cx)**2+(cen[j][1]-cy)**2)[1:3]
        for j in ds:
            x2,y2=cen[j]
            mid=(x2,cy) if random.random()<0.5 else (cx,y2)
            d.line([px(cx,cy),px(*mid),px(x2,y2)],fill=PAL[2],width=int(tw),joint='curve')
    # a ground-pour hatch in free areas
    for k in range(60):
        x=random.uniform(BX0,BX1);y=random.uniform(8,BY1);l=random.uniform(4,18)
        d.line([px(x,y),px(x+l,y)],fill=PAL[9],width=int(tw*1.4))
    # footprints: silkscreen outline, pads, holes
    font=ImageFont.load_default(size=int(2.6*sc*S/1.0))
    n=0
    for lo,hi in comps:
        x0,y0,x1,y1=lo[0],lo[1],hi[0],hi[1];w=x1-x0;h=y1-y0;n+=1
        a=px(x0,y0);b=px(x1,y1);box=[min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1])]
        if max(w,h)>7 and min(w,h)>2.5:                           # IC / connector: pads along both long sides, outline
            horiz=w>=h;pitch=2.54;cnt=max(2,int((w if horiz else h)/pitch))
            for k in range(cnt):
                t=(k+0.5)/cnt
                for side in (0,1):
                    if horiz: xx=x0+t*w;yy=(y0-0.9) if side==0 else (y1+0.9)
                    else: yy=y0+t*h;xx=(x0-0.9) if side==0 else (x1+0.9)
                    pxx,pyy=px(xx,yy);r=0.65*sc*S
                    d.ellipse([pxx-r,pyy-r,pxx+r,pyy+r],fill=PAL[3]);r2=r*0.45;d.ellipse([pxx-r2,pyy-r2,pxx+r2,pyy+r2],fill=PAL[6])
            d.rectangle(box,outline=PAL[4],width=max(1,int(0.25*sc*S)))
            if n%2==0: d.text((box[0]+2*S,box[1]+1*S),'U%d'%n,fill=PAL[4],font=font)
        else:                                                     # small part (resistor, capacitor): two pads at the ends
            horiz=w>=h
            for sgn in (-1,1):
                cx_,cy_=((x0+x1)/2+(sgn*(w/2+0.4) if horiz else 0),(y0+y1)/2+(0 if horiz else sgn*(h/2+0.4)))
                pxx,pyy=px(cx_,cy_);r=0.8*sc*S
                d.ellipse([pxx-r,pyy-r,pxx+r,pyy+r],fill=PAL[3]);r2=r*0.4;d.ellipse([pxx-r2,pyy-r2,pxx+r2,pyy+r2],fill=PAL[6])
            if max(w,h)>4: d.ellipse([(box[0]+box[2])/2-(box[2]-box[0])*0.6,(box[1]+box[3])/2-(box[3]-box[1])*0.6,(box[0]+box[2])/2+(box[2]-box[0])*0.6,(box[1]+box[3])/2+(box[3]-box[1])*0.6],outline=PAL[4],width=max(1,int(0.2*sc*S)))
    # vias
    for k in range(120):
        x=random.uniform(BX0+3,BX1-3);y=random.uniform(10,BY1-3);pxx,pyy=px(x,y);r=0.5*sc*S
        d.ellipse([pxx-r,pyy-r,pxx+r,pyy+r],fill=PAL[7]);r2=r*0.45;d.ellipse([pxx-r2,pyy-r2,pxx+r2,pyy+r2],fill=PAL[6])
    # silkscreen text
    big=ImageFont.load_default(size=int(4.2*sc*S))
    txt={'GF1':('ULTRASOUND','REV 3.4','ADVANCED GRAVIS COMPUTER TECHNOLOGY LTD.'),'IW':('ULTRASOUND PnP','V3.0','ADVANCED GRAVIS COMPUTER TECHNOLOGY LTD.')}[tag]
    d.text((6*sc*S,(BY1-30)*sc*S),txt[0],fill=PAL[4],font=big);d.text((6*sc*S,(BY1-23)*sc*S),txt[1],fill=PAL[4],font=big);d.text((6*sc*S,(BY1-14)*sc*S),txt[2],fill=PAL[4],font=font)
    # gold fingers on the card edge (the quads below y = 8)
    for (x0,y0,x1,y1) in QUADS[1:]:
        a=px(x0,y0);b=px(x1,y1)
        d.rectangle([min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1])],fill=PAL[6])
        x=x0+0.6
        while x<x1-1.4:
            a=px(x,y0);b=px(x+1.5,y1);d.rectangle([min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1])],fill=PAL[5]);x+=2.54
    im=im.resize((W,H),Image.LANCZOS)
    pal=Image.new('P',(1,1));flat=[c for col in PAL for c in col]+[0]*(768-3*len(PAL));pal.putpalette(flat)
    q=np.asarray(im.quantize(palette=pal,dither=Image.Dither.NONE)).astype(np.uint8)
    if os.environ.get('BACK_PREVIEW'): Image.fromarray(q,'P').convert('RGB') if False else im.save(os.environ['BACK_PREVIEW']+tag+'.png')
    return q,[(core[0][0],8.0,core[1][0],core[1][1])]+[(a,b,c,dd) for a,b,c,dd in QUADS[1:]],PAL

def bake(tag,glb,texw):
    j,b=load(U+glb)
    M=np.array(j['nodes'][0]['matrix']).reshape(4,4).T
    V=[];VC=[];I=[];pal=[];TV=[];TUV=[];TI=[];PRIMS=[];QUADS=[]
    for ni,n in enumerate(j['nodes']):
        m=j['meshes'][n['mesh']];pr=m['primitives'][0];at=pr['attributes']
        p_,_=acc(j,b,at['POSITION']);ia,_=acc(j,b,pr['indices'])
        p_=p_.astype(np.float64)
        if 'TEXCOORD_0' in at:
            uv,_=acc(j,b,at['TEXCOORD_0']);base=len(TV)
            for k in range(len(p_)): TV.append(p_[k]);TUV.append(uv[k])
            TI+= [base+int(i) for i in ia]
            QUADS.append((p_[:,0].min(),p_[:,1].min(),p_[:,0].max(),p_[:,1].max()))
        else:
            c,_=acc(j,b,at['COLOR_0']);col=tuple(int(x) for x in c[0][:3])
            if col not in pal: pal.append(col)
            base=len(V)
            for k in range(len(p_)): V.append(p_[k]);VC.append(pal.index(col))
            I+=[base+int(i) for i in ia]
            PRIMS.append((p_.min(0),p_.max(0),col))
    V=np.array(V);TV=np.array(TV);TUV=np.array(TUV,dtype=np.float64)
    allp=np.vstack([V,TV]);lo=allp.min(0);hi=allp.max(0)
    cx,cy=(lo[0]+hi[0])/2,(lo[1]+hi[1])/2;cz=0.8
    half=(hi[0]-lo[0])/2
    def quant(P): return np.round(np.column_stack([(P[:,0]-cx)/half,(P[:,1]-cy)/half,(P[:,2]-cz)/half])*Q).astype(int).flatten()
    img_bv=j['bufferViews'][j['images'][0]['bufferView']];img=Image.open(io.BytesIO(b[img_bv.get('byteOffset',0):img_bv.get('byteOffset',0)+img_bv['byteLength']])).convert('RGB');th=int(round(texw*img.height/img.width));img=img.resize((texw,th),Image.LANCZOS)
    a=np.asarray(img).astype(np.uint16);t565=((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)
    bimg,bq,bpal=make_back(tag,PRIMS,QUADS)
    bv=[];buv=[];bi=[]
    for (x0,y0,x1,y1) in bq:
        base=len(bv)
        for (x,y) in ((x0,y0),(x1,y0),(x1,y1),(x0,y1)):
            bv.append((x,y,-0.03));buv.append((1-(x-BX0)/(BX1-BX0),1-(y-BY0)/(BY1-BY0)))
        bi+=[base,base+1,base+2,base,base+2,base+3]
    bv=np.array(bv);buv=np.array(buv)
    print(tag,'verts',len(V),'tris',len(I)//3,'palette',len(pal),'texverts',len(TV),'tex',texw,th,'half',half,'zmax',hi[2],'aspect',(hi[0]-lo[0])/(hi[1]-lo[1]))
    s='/* %s: baked from the 3D model, see tools/bake_card3d.py */\n#define C3_%s_NV %d\n#define C3_%s_NT %d\n#define C3_%s_NTV %d\n#define C3_%s_NTT %d\n#define C3_%s_TW %d\n#define C3_%s_TH %d\n#define C3_%s_ASPECT %.4ff\n'%(tag,tag,len(V),tag,len(I)//3,tag,len(TV),tag,len(TI)//3,tag,texw,tag,th,tag,(hi[0]-lo[0])/(hi[1]-lo[1]))
    s+=arr('c3_%s_v'%tag,'int16_t',quant(V))+arr('c3_%s_vc'%tag,'uint8_t',VC,32)+arr('c3_%s_i'%tag,'uint16_t',I)
    s+=arr('c3_%s_pal'%tag,'uint8_t',[x for c in pal for x in c])
    s+=arr('c3_%s_tv'%tag,'int16_t',quant(TV))+arr('c3_%s_tuv'%tag,'uint16_t',np.round(TUV.flatten()*65535).astype(int))+arr('c3_%s_ti'%tag,'uint16_t',TI)
    s+=arr('c3_%s_tex'%tag,'uint16_t',t565.flatten(),12)
    s+='#define C3_%s_BW %d\n#define C3_%s_BH %d\n#define C3_%s_NBV %d\n#define C3_%s_NBT %d\n'%(tag,bimg.shape[1],tag,bimg.shape[0],tag,len(bv),tag,len(bi)//3)
    s+=arr('c3_%s_bv'%tag,'int16_t',quant(bv))+arr('c3_%s_buv'%tag,'uint16_t',np.round(buv.flatten()*65535).astype(int))+arr('c3_%s_bi'%tag,'uint16_t',bi)
    s+=arr('c3_%s_bpal'%tag,'uint8_t',[x for c in bpal for x in c])+arr('c3_%s_bpix'%tag,'uint8_t',pack4(bimg),24)
    return s

# ---- EWS64: a GLB with one material colour per mesh (no vertex colours), the board lying in the XZ plane (y up) ----
def components(P,I):
    """connected pieces of an indexed mesh (vertices at the same position count as one): list of (lo, hi)"""
    key={};par=list(range(len(P)))
    def f(a):
        while par[a]!=a: par[a]=par[par[a]];a=par[a]
        return a
    for i,p in enumerate(P):
        k=tuple(np.round(p,2))
        if k in key: par[f(i)]=f(key[k])
        else: key[k]=i
    for t in I.reshape(-1,3):
        a,b2,c=[f(int(x)) for x in t];par[f(b2)]=a;par[f(c)]=a
    groups={}
    for i in range(len(P)): groups.setdefault(f(i),[]).append(i)
    return [(P[g].min(0),P[g].max(0)) for g in groups.values()]

PAL_EWS=[(31,23,15),(22,16,11),(54,40,27),(205,205,198),(228,226,214),(216,172,62),(10,8,6),(160,140,100),(162,162,156),(40,30,20),(18,13,9),(70,52,36),(140,140,135),(238,200,110),(26,19,12),(14,10,7)]
PAL_AWE=[(52,142,74),(38,110,58),(92,176,104),(205,205,198),(236,236,226),(216,172,62),(14,34,20),(172,152,112),(162,162,156),(40,122,62),(24,86,44),(70,158,88),(140,140,135),(238,200,110),(30,100,52),(18,70,36)]      # the AWE32: green like its solder mask
def make_back_generic(tag,comps,board,fingers,pal,lines):
    """comps: [(lo,hi)] of the parts (card coordinates: x, y, z up); board = (x0,y0,x1,y1) of the whole image; fingers = edge connector rectangles"""
    global BX0,BX1,BY0,BY1
    from PIL import ImageDraw,ImageFont
    import random
    random.seed(11)
    BX0,BY0,BX1,BY1=board
    W=480;sc=W/(BX1-BX0);H=int(round((BY1-BY0)*sc));S=3
    im=Image.new('RGB',(W*S,H*S),pal[0]);d=ImageDraw.Draw(im)
    def px(x,y): return ((BX1-x)*sc*S,(BY1-y)*sc*S)
    cen=[((l[0]+h[0])/2,(l[1]+h[1])/2) for l,h in comps]
    tw=max(1.0,0.5*sc)*S
    for i,(cx,cy) in enumerate(cen):
        ds=sorted(range(len(cen)),key=lambda j:(cen[j][0]-cx)**2+(cen[j][1]-cy)**2)[1:3]
        for j in ds:
            x2,y2=cen[j];mid=(x2,cy) if random.random()<0.5 else (cx,y2)
            d.line([px(cx,cy),px(*mid),px(x2,y2)],fill=pal[2],width=int(tw),joint='curve')
    for k in range(60):
        x=random.uniform(BX0,BX1);y=random.uniform(BY0,BY1);l=random.uniform(4,18)
        d.line([px(x,y),px(x+l,y)],fill=pal[9],width=int(tw*1.4))
    font=ImageFont.load_default(size=int(2.6*sc*S));n=0
    for lo,hi in comps:
        x0,y0,x1,y1=lo[0],lo[1],hi[0],hi[1];w=x1-x0;h=y1-y0;n+=1
        a=px(x0,y0);b=px(x1,y1);box=[min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1])]
        if max(w,h)>7 and min(w,h)>2.5:
            horiz=w>=h;cnt=max(2,int((w if horiz else h)/2.54))
            for k in range(cnt):
                t=(k+0.5)/cnt
                for side in (0,1):
                    if horiz: xx=x0+t*w;yy=(y0-0.9) if side==0 else (y1+0.9)
                    else: yy=y0+t*h;xx=(x0-0.9) if side==0 else (x1+0.9)
                    pxx,pyy=px(xx,yy);r=0.65*sc*S;d.ellipse([pxx-r,pyy-r,pxx+r,pyy+r],fill=pal[3]);r2=r*0.45;d.ellipse([pxx-r2,pyy-r2,pxx+r2,pyy+r2],fill=pal[6])
            d.rectangle(box,outline=pal[4],width=max(1,int(0.25*sc*S)))
            if n%2==0: d.text((box[0]+2*S,box[1]+1*S),'U%d'%n,fill=pal[4],font=font)
        else:
            horiz=w>=h
            for sgn in (-1,1):
                cx_,cy_=((x0+x1)/2+(sgn*(w/2+0.4) if horiz else 0),(y0+y1)/2+(0 if horiz else sgn*(h/2+0.4)))
                pxx,pyy=px(cx_,cy_);r=0.8*sc*S;d.ellipse([pxx-r,pyy-r,pxx+r,pyy+r],fill=pal[3]);r2=r*0.4;d.ellipse([pxx-r2,pyy-r2,pxx+r2,pyy+r2],fill=pal[6])
            if max(w,h)>4: d.ellipse([(box[0]+box[2])/2-(box[2]-box[0])*0.6,(box[1]+box[3])/2-(box[3]-box[1])*0.6,(box[0]+box[2])/2+(box[2]-box[0])*0.6,(box[1]+box[3])/2+(box[3]-box[1])*0.6],outline=pal[4],width=max(1,int(0.2*sc*S)))
    for k in range(120):
        x=random.uniform(BX0+3,BX1-3);y=random.uniform(BY0+3,BY1-3);pxx,pyy=px(x,y);r=0.5*sc*S
        d.ellipse([pxx-r,pyy-r,pxx+r,pyy+r],fill=pal[7]);r2=r*0.45;d.ellipse([pxx-r2,pyy-r2,pxx+r2,pyy+r2],fill=pal[6])
    big=ImageFont.load_default(size=int(4.2*sc*S))
    d.text((6*sc*S,(BY1-BY0-26)*sc*S),lines[0],fill=pal[4],font=big);d.text((6*sc*S,(BY1-BY0-19)*sc*S),lines[1],fill=pal[4],font=font)
    for (x0,y0,x1,y1) in fingers:
        d.rectangle([min(px(x0,y0)[0],px(x1,y1)[0]),min(px(x0,y0)[1],px(x1,y1)[1]),max(px(x0,y0)[0],px(x1,y1)[0]),max(px(x0,y0)[1],px(x1,y1)[1])],fill=pal[5])
    im=im.resize((W,H),Image.LANCZOS)
    pimg=Image.new('P',(1,1));pimg.putpalette([c for col in pal for c in col]+[0]*(768-3*len(pal)))
    return np.asarray(im.quantize(palette=pimg,dither=Image.Dither.NONE)).astype(np.uint8)

def bake_ews(tag,glb,texw,part_names=('cap_dark','chip_black','plastic_white','plastic_black','metal','pcb_green'),lines=('EWS64','AUDIOSYSTEM'),auto_outline=False,classify=None,scale=1.0,back_pal=None):
    """classify(node name) -> 'board' | 'finger' | 'part' | None replaces the name lists (a model with other node names); scale turns the model units into mm"""
    j,b=load(U+glb)
    def tr(P): return np.column_stack([P[:,0],-P[:,2],P[:,1]])*scale          # card coordinates: x, y = -z, z = up (a proper rotation)
    V=[];VC=[];I=[];pal=[];TV=[];TUV=[];TI=[];comps=[];fingers=[];board=None;outline=[]
    names=[n.get('name') for n in j['nodes']]
    for ni,n in enumerate(j['nodes']):
        m=j['meshes'][n['mesh']];pr=m['primitives'][0];at=pr['attributes']
        p_,_=acc(j,b,at['POSITION']);ia,_=acc(j,b,pr['indices']);P=tr(p_.astype(np.float64));mat=j['materials'][pr['material']]
        if 'TEXCOORD_0' in at:
            uv,_=acc(j,b,at['TEXCOORD_0']);base=len(TV)
            for k in range(len(P)): TV.append(P[k]);TUV.append(uv[k])
            TI+=[base+int(i) for i in ia]
        else:
            f=mat['pbrMetallicRoughness']['baseColorFactor'];col=tuple(int(round(255*x)) for x in f[:3])
            if col not in pal: pal.append(col)
            base=len(V)
            for k in range(len(P)): V.append(P[k]);VC.append(pal.index(col))
            I+=[base+int(i) for i in ia]
            cps=components(P,ia.astype(int))
            kind=classify(names[ni]) if classify else ('board' if names[ni]=='pcb_bottom' else 'finger' if names[ni]=='gold' else 'part' if names[ni] in part_names else None)
            if kind=='board':
                board=(P[:,0].min(),P[:,1].min(),P[:,0].max(),P[:,1].max())
                outline=[(lo[0],lo[1],hi[0],hi[1]) for lo,hi in cps]                  # the rectangles of the board bottom (the main part and the connector strip)
            elif kind=='finger':
                for lo,hi in cps:                                              # the edge connector fingers: thin pieces lying on the board
                    if hi[2]<=1.7 and (lo[2]>=1.5 or classify) and (hi[1]-lo[1])>3: fingers.append((lo[0],lo[1],hi[0],hi[1]))
            elif kind=='part':
                comps+= [(lo,hi) for lo,hi in cps if hi[2]>1.7]
    if classify and fingers:                                                     # a model whose edge connector sticks out of the board: the back picture and its outline cover the strip too
        board=(min(board[0],min(f[0] for f in fingers)),min(board[1],min(f[1] for f in fingers)),max(board[2],max(f[2] for f in fingers)),max(board[3],max(f[3] for f in fingers)))
        outline=outline+[f for f in fingers]
    V=np.array(V);TV=np.array(TV);TUV=np.array(TUV,dtype=np.float64)
    allp=np.vstack([V,TV]);lo=allp.min(0);hi=allp.max(0)
    cx,cy=(lo[0]+hi[0])/2,(lo[1]+hi[1])/2;cz=0.8;half=(hi[0]-lo[0])/2
    def quant(P): return np.round(np.column_stack([(P[:,0]-cx)/half,(P[:,1]-cy)/half,(P[:,2]-cz)/half])*Q).astype(int).flatten()
    img_bv=j['bufferViews'][j['images'][0]['bufferView']];img=Image.open(io.BytesIO(b[img_bv.get('byteOffset',0):img_bv.get('byteOffset',0)+img_bv['byteLength']])).convert('RGB')
    th=int(round(texw*img.height/img.width));img=img.resize((texw,th),Image.LANCZOS)
    a=np.asarray(img).astype(np.uint16);t565=((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)
    bpal=back_pal or PAL_EWS
    bimg=make_back_generic(tag,comps,board,fingers,bpal,lines)
    # the back picture: the board outline (the part with fingers is narrower) as two quads
    bq=[(board[0],-43.2,board[2],board[3]),(-36.6,board[1],board[2],-43.2)]       # the board outline: the main part and the strip with the edge connector
    if auto_outline: bq=outline                                                      # other boards: the rectangles of their own bottom
    bv=[];buv=[];bi=[]
    for (x0,y0,x1,y1) in bq:
        base=len(bv)
        for (x,y) in ((x0,y0),(x1,y0),(x1,y1),(x0,y1)):
            bv.append((x,y,-0.03));buv.append((1-(x-BX0)/(BX1-BX0),1-(y-BY0)/(BY1-BY0)))
        bi+=[base,base+1,base+2,base,base+2,base+3]
    bv=np.array(bv);buv=np.array(buv)
    print(tag,'verts',len(V),'tris',len(I)//3,'palette',len(pal),'texverts',len(TV),'textris',len(TI)//3,'tex',texw,th,'half',half,'aspect',(hi[0]-lo[0])/(hi[1]-lo[1]),'parts',len(comps),'fingers',len(fingers),'board',board)
    s='/* %s: baked from the 3D model, see tools/bake_card3d.py */\n#define C3_%s_NV %d\n#define C3_%s_NT %d\n#define C3_%s_NTV %d\n#define C3_%s_NTT %d\n#define C3_%s_TW %d\n#define C3_%s_TH %d\n#define C3_%s_ASPECT %.4ff\n'%(tag,tag,len(V),tag,len(I)//3,tag,len(TV),tag,len(TI)//3,tag,texw,tag,th,tag,(hi[0]-lo[0])/(hi[1]-lo[1]))
    s+=arr('c3_%s_v'%tag,'int16_t',quant(V))+arr('c3_%s_vc'%tag,'uint8_t',VC,32)+arr('c3_%s_i'%tag,'uint16_t',I)
    s+=arr('c3_%s_pal'%tag,'uint8_t',[x for c in pal for x in c])
    s+=arr('c3_%s_tv'%tag,'int16_t',quant(TV))+arr('c3_%s_tuv'%tag,'uint16_t',np.round(TUV.flatten()*65535).astype(int))+arr('c3_%s_ti'%tag,'uint16_t',TI)
    s+=arr('c3_%s_tex'%tag,'uint16_t',t565.flatten(),12)
    s+='#define C3_%s_BW %d\n#define C3_%s_BH %d\n#define C3_%s_NBV %d\n#define C3_%s_NBT %d\n'%(tag,bimg.shape[1],tag,bimg.shape[0],tag,len(bv),tag,len(bi)//3)
    s+=arr('c3_%s_bv'%tag,'int16_t',quant(bv))+arr('c3_%s_buv'%tag,'uint16_t',np.round(buv.flatten()*65535).astype(int))+arr('c3_%s_bi'%tag,'uint16_t',bi)
    s+=arr('c3_%s_bpal'%tag,'uint8_t',[x for c in bpal for x in c])+arr('c3_%s_bpix'%tag,'uint8_t',pack4(bimg),24)
    return s

if __name__=='__main__':
    out='/* 3D models of the Gravis Ultrasound (GF1, rev 3.4) and Ultrasound PnP (AMD InterWave) cards and the Terratec EWS64. GPL-3.0-or-later */\n#include <stdint.h>\n'
    out+=bake('GF1','gus_ultrasound_rev34.glb',320)+bake('IW','gus_pnp_v30.glb',320)+bake_ews('EWS','ews64.glb',320)
    if os.path.exists(U+'adlib_gold.glb'):                                       # the AdLib Gold card (Yamaha OPL3): the FM formats
        out+=bake_ews('GOLD','adlib_gold.glb',320,('cap_silver','cap_gold','chip_black','plastic_white','plastic_black','metal'),('ADLIB GOLD','1000 MUSIC CARD'),True)
    open(sys.argv[2] if len(sys.argv)>2 else 'src/card3d_data.h','w').write(out);print(len(out)//1024,'KB')
