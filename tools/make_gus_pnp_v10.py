# usage: python3 tools/make_gus_pnp_v10.py PHOTO.jpg OUT.glb
# Builds the model of the Gravis UltraSound PnP (V1.0) for bake_card3d.py from a straight photo of the card:
# the photo is the texture of the board, the big chips, the connectors and the regulators are boxes placed by hand (photo pixels).
# Photo: "Gravis UltraSound PnP Pro V1.0" by TMg, Wikimedia Commons, CC BY-SA 2.0 de (https://creativecommons.org/licenses/by-sa/2.0/de/deed.de); cropped, scaled and turned into a 3D model.
import sys,io,json,struct,numpy as np
from PIL import Image
im=Image.open(sys.argv[1]).convert('RGB')
X0,X1,Y0,Y1,YF=49,1471,49,720,770                  # board in the photo (px), and where the gold fingers end
s=206.4/(X1-X0); YB=(YF-Y1)*s                       # model units per px; height of the finger strips
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
FR4=(150,32,28); CHIP=(28,26,26); SIL=(162,162,156); TAN=(172,152,112)
box(0,YB,0,206.4,my(Y0),1.6,FR4)                       # the board
quad(X0,Y0,X1,Y1)                                      # its photo
quad(396,Y1,714,YF); quad(769,Y1,1298,YF)               # the two groups of gold fingers (the gap is empty)
PARTS=[(338,68,738,130,6,CHIP),(480,207,658,286,3.5,CHIP),(310,258,437,449,4.5,CHIP),(757,228,966,455,3.5,CHIP),(505,424,572,511,3,CHIP),
 (335,572,412,664,3,CHIP),(490,572,557,671,3,CHIP),(674,603,763,664,3,CHIP),(830,615,1021,683,4,CHIP),(1154,594,1243,658,3,CHIP),(1027,621,1083,664,2.5,CHIP),
 (1073,185,1150,308,6,CHIP),(1070,375,1138,517,6,CHIP),(1175,375,1243,517,6,CHIP),(1230,74,1298,123,3,CHIP),(1230,264,1298,326,3,CHIP),
 (1375,166,1471,228,9,CHIP),(1375,258,1471,320,9,CHIP),(1375,357,1471,418,9,CHIP),(1388,449,1471,720,11,SIL)]
for k in (0,1,2):                                      # the memory chips on the two modules
    y0,y1=[(154,246),(351,424),(492,578)][k]
    box(mx(76),my(y1),1.6,mx(138),my(y0),4.6,CHIP); box(mx(166),my(y1),1.6,mx(228),my(y0),4.6,CHIP)
for (x0,y0,x1,y1,h,c) in PARTS: box(mx(x0),my(y1),1.6,mx(x1),my(y0),1.6+h,c)
box(206.6,-4,-3,207.6,YB+(Y1-Y0)*s+4,15.4,SIL)         # the slot bracket
buf=io.BytesIO(); im.save(buf,'JPEG',quality=92); ib=add(buf.getvalue())
js={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':list(range(len(nodes)))}],'nodes':nodes,'meshes':meshes,'accessors':accs,'bufferViews':views,'buffers':[{'byteLength':0}],'images':[{'bufferView':ib,'mimeType':'image/jpeg'}]}
bd=b''.join(bufs); bd+=b'\0'*((4-len(bd)%4)%4); js['buffers'][0]['byteLength']=len(bd)
jd=json.dumps(js).encode(); jd+=b' '*((4-len(jd)%4)%4)
open(sys.argv[2],'wb').write(struct.pack('<III',0x46546C67,2,28+len(jd)+len(bd))+struct.pack('<II',len(jd),0x4E4F534A)+jd+struct.pack('<II',len(bd),0x004E4942)+bd)
print('ok',len(nodes),'nodes',W,H)
