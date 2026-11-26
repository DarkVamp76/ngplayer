# usage: python3 tools/make_awe32_photo.py PHOTO.jpg OUT.glb
# Builds the model of the Sound Blaster AWE32 (CT3990) for bake_card3d.py from a straight photo of the card (taken by the maintainer):
# the photo (a little darker) is the texture of the board, the big chips, the connectors and the sockets are boxes placed by hand (photo pixels).
import sys,io,json,struct,numpy as np
from PIL import Image
from PIL import ImageEnhance
im=ImageEnhance.Contrast(ImageEnhance.Brightness(Image.open(sys.argv[1]).convert('RGB')).enhance(0.74)).enhance(1.3)
X0,X1,Y0,Y1=95,1448,290,728                  # board in the photo (px), and where the gold fingers end
s=206.4/(X1-X0); YF=758; YB=8.0              # the gold fingers end at row 758 of the photo                       # model units per px; height of the finger strips
im=im.crop((X0,Y0,X1,YF)); W,H=im.size
def mx(px): return (px-X0)*s
def my(py): return YB+(Y1-py)*s                     # y up; the board bottom edge sits at YB
def uv(px,py): return ((px-X0)/W,(py-Y0)/H)
bufs=[];views=[];accs=[];nodes=[];meshes=[]
def add(data,target=None):
    while len(b''.join(bufs))%4: bufs.append(b'\0')
    off=len(b''.join(bufs)); bufs.append(data); views.append({'buffer':0,'byteOffset':off,'byteLength':len(data)}); return len(views)-1
def accessor(arr,ct,typ,mm=False):
    bv=add(arr.tobytes()); a={'bufferView':bv,'componentType':ct,'count':len(arr),'type':typ}
    if mm: a['min']=arr.min(0).tolist(); a['max']=arr.max(0).tolist()
    accs.append(a); return len(accs)-1
BOXI=np.array([0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,1,5,6,1,6,2,2,6,7,2,7,3,3,7,4,3,4,0],dtype=np.uint32)
def node(P,I,color=None,UV=None):
    at={'POSITION':accessor(np.array(P,dtype=np.float32),5126,'VEC3',True)}
    if UV is not None: at['TEXCOORD_0']=accessor(np.array(UV,dtype=np.float32),5126,'VEC2')
    else: at['COLOR_0']=accessor(np.array([list(color)+[255]]*len(P),dtype=np.uint8),5121,'VEC4')
    meshes.append({'primitives':[{'attributes':at,'indices':accessor(np.array(I,dtype=np.uint32),5125,'SCALAR',True),'mode':4}]})
    nodes.append({'name':'n%d'%len(nodes),'mesh':len(meshes)-1,'matrix':[1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1]})
def box(x0,y0,z0,x1,y1,z1,col): node([(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)],BOXI,col)
def quad(px0,py0,px1,py1,z=1.6):
    P=[(mx(px0),my(py1),z),(mx(px1),my(py1),z),(mx(px1),my(py0),z),(mx(px0),my(py0),z)]
    node(P,[0,1,2,0,2,3],None,[uv(px0,py1),uv(px1,py1),uv(px1,py0),uv(px0,py0)])
FR4=(52,142,74); CHIP=(28,26,26); SIL=(162,162,156)
box(0,YB,0,206.4,my(Y0),1.6,FR4)
quad(X0,Y0,X1,Y1)
quad(826,Y1,1030,YF); quad(1050,Y1,1392,YF)               # the two groups of gold fingers
PARTS=[(560,315,592,372,4,SIL),(610,338,678,415,3,CHIP),(858,338,966,445,3.5,CHIP),(360,447,468,495,3,CHIP),
 (355,543,482,668,4,CHIP),(695,530,820,668,4,CHIP),(528,453,585,518,3,CHIP),(180,440,238,485,2.5,CHIP),(272,450,328,495,2.5,CHIP),
 (180,505,238,552,2.5,CHIP),(180,585,236,628,2.5,CHIP),(180,650,236,696,2.5,CHIP),(272,520,310,548,2.5,CHIP),(277,578,322,678,3,CHIP),
 (535,568,583,648,4,SIL),(828,490,912,540,3.5,SIL),(1078,350,1180,375,6,CHIP),(1112,290,1166,345,7,CHIP),(122,460,146,670,5,CHIP),
 (1228,300,1258,362,5,SIL),(1010,470,1090,505,2.5,CHIP),(1280,590,1338,615,2.5,CHIP),(1280,625,1338,640,2.5,CHIP),(1280,650,1338,690,2.5,CHIP),
 (960,610,1010,690,3,CHIP),(1340,330,1395,365,3,CHIP),(1050,640,1100,670,2.5,CHIP),
 (1440,335,1448,410,10,CHIP),(1440,415,1448,490,10,CHIP),(1440,495,1448,570,10,CHIP),(1380,580,1448,760,11,SIL)]
for (x0,y0,x1,y1,h,c) in PARTS: box(mx(x0),my(y1),1.6,mx(x1),my(y0),1.6+h,c)
for (y0,y1) in ((298,338),(350,388)):                   # the memory chips on the two modules
    for (a,b) in ((188,270),(310,390),(435,488)): box(mx(a),my(y1),2.0,mx(b),my(y0),4.0,CHIP)
box(206.6,-4,-3,207.6,YB+(Y1-Y0)*s+4,15.4,SIL)         # the slot bracket
buf=io.BytesIO(); im.save(buf,'JPEG',quality=92); ib=add(buf.getvalue())
js={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':list(range(len(nodes)))}],'nodes':nodes,'meshes':meshes,'accessors':accs,'bufferViews':views,'buffers':[{'byteLength':0}],'images':[{'bufferView':ib,'mimeType':'image/jpeg'}]}
bd=b''.join(bufs); bd+=b'\0'*((4-len(bd)%4)%4); js['buffers'][0]['byteLength']=len(bd)
jd=json.dumps(js).encode(); jd+=b' '*((4-len(jd)%4)%4)
open(sys.argv[2],'wb').write(struct.pack('<III',0x46546C67,2,28+len(jd)+len(bd))+struct.pack('<II',len(jd),0x4E4F534A)+jd+struct.pack('<II',len(bd),0x004E4942)+bd)
print('ok',len(nodes),'nodes',W,H)
