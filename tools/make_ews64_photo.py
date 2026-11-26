# usage: python3 tools/make_ews64_photo.py PHOTO.jpg OUT.glb
# Builds the model of the Terratec EWS64 for bake_card3d.py from a straight photo of the card (taken by the maintainer):
# the photo  is the texture of the board, the big chips, the connectors and the sockets are boxes placed by hand (photo pixels).
import sys,io,json,struct,numpy as np
from PIL import Image
from PIL import ImageEnhance
im=ImageEnhance.Brightness(Image.open(sys.argv[1]).convert('RGB')).enhance(1.05)
X0,X1,Y0,Y1=47,1455,38,640                  # board in the photo (px), and where the gold fingers end
s=206.4/(X1-X0); YF=685; YB=8.0                       # model units per px; height of the finger strips
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
FR4=(31,23,15); CHIP=(28,26,26); SIL=(162,162,156); WHT=(205,205,198); GOLD=(216,172,62)
box(0,YB,0,206.4,my(Y0),1.6,FR4)
quad(X0,Y0,X1,Y1)
quad(506,Y1,782,YF); quad(818,Y1,1288,YF)                  # the two groups of gold fingers
PARTS=[(778,60,845,135,8,WHT),(848,20,918,95,8,WHT),(1035,78,1245,112,6,CHIP),(570,240,650,320,3,CHIP),(238,462,345,560,3.5,CHIP),
 (392,466,455,582,4,SIL),(568,445,700,590,4,CHIP),(835,455,965,568,4,CHIP),(283,262,358,340,2.5,CHIP),(396,262,470,340,2.5,CHIP),
 (240,372,300,405,2.5,CHIP),(318,372,372,405,2.5,CHIP),(150,410,222,440,2.5,CHIP),(418,375,470,425,2.5,CHIP),(68,292,272,330,6,CHIP),
 (68,455,150,615,6,CHIP),(1378,468,1455,680,11,CHIP),(1405,130,1455,200,10,CHIP),(1405,205,1455,275,10,CHIP),(1405,285,1455,350,10,CHIP),
 (1405,360,1455,432,10,CHIP),(1300,468,1345,568,4,CHIP),(958,222,1075,262,2.5,CHIP),(1160,335,1222,385,2.5,CHIP),(885,312,948,360,2.5,CHIP)]
for (x0,y0,x1,y1,h,c) in PARTS: box(mx(x0),my(y1),1.6,mx(x1),my(y0),1.6+h,c)
for k in range(8):                                      # the memory chips on the module
    a=110+k*77; box(mx(a),my(190),2.0,mx(a+72),my(78),3.8,CHIP)
box(206.6,-4,-3,207.6,YB+(Y1-Y0)*s+4,15.4,GOLD)         # the slot bracket
buf=io.BytesIO(); im.save(buf,'JPEG',quality=92); ib=add(buf.getvalue())
js={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':list(range(len(nodes)))}],'nodes':nodes,'meshes':meshes,'accessors':accs,'bufferViews':views,'buffers':[{'byteLength':0}],'images':[{'bufferView':ib,'mimeType':'image/jpeg'}]}
bd=b''.join(bufs); bd+=b'\0'*((4-len(bd)%4)%4); js['buffers'][0]['byteLength']=len(bd)
jd=json.dumps(js).encode(); jd+=b' '*((4-len(jd)%4)%4)
open(sys.argv[2],'wb').write(struct.pack('<III',0x46546C67,2,28+len(jd)+len(bd))+struct.pack('<II',len(jd),0x4E4F534A)+jd+struct.pack('<II',len(bd),0x004E4942)+bd)
print('ok',len(nodes),'nodes',W,H)
