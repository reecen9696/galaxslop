"""Restricted J3D exporter for Barney's rigid, opaque, vertex-coloured meshes.

Written against the local game's J3D loader structures. Not a general BMD exporter.
All transforms and animation use the same bone order and exact evaluated matrices.
"""
import bpy, math, struct, json
from pathlib import Path
from collections import defaultdict
from mathutils import Matrix

OUT=Path(__file__).resolve().parents[1]  # the character folder (this file is in source/)
P=lambda fmt,*v:struct.pack('>'+fmt,*v)

def align(data,n=32):
    data.extend(bytes((-len(data))%n))

def add(data,blob,n=4):
    align(data,n);off=len(data);data.extend(blob);return off

def section(tag,body):
    body[:4]=tag.encode();align(body);struct.pack_into('>I',body,4,len(body));return bytes(body)

def j3d(magic,sections):
    payload=b''.join(sections)
    return magic+P('II',32+len(payload),len(sections))+b'SVR3'+b'\xff'*12+payload

def ntab(names):
    data=bytearray(P('HH',len(names),0xffff)+bytes(4*len(names)))
    for i,n in enumerate(names):
        h=0
        for c in n.encode('ascii'):h=(h*3+c)&65535
        struct.pack_into('>HH',data,4+4*i,h,len(data));data.extend(n.encode('ascii')+b'\0')
    return data

def mat3(names):
    # GX raster colour, diffuse lighting, opaque depth-tested output; no texture lookup.
    count=len(names);d=bytearray(0x84);struct.pack_into('>HH',d,8,count,0xffff)
    offsets=[0]*30
    def table(i,b):offsets[i]=add(d,b)
    init=bytearray(b'\xff'*0x14c)
    init[:8]=bytes([1,0,0,0,0,0,0,0])
    for o in [8,10,12,20,22,0xbc,0xe4,0x104,0x124,0x126,0x128,0x12a,0x144,0x146,0x148,0x14a]:struct.pack_into('>H',init,o,0)
    struct.pack_into('>H',init,14,1) # independent alpha channel: unlit vertex alpha
    for o in range(0x94,0x9c,2):struct.pack_into('>H',init,o,0)
    for o in range(0xdc,0xe4,2):struct.pack_into('>H',init,o,0)
    init[0x9c:0xbc]=bytes([0x0c]*16+[0x1c]*16)
    table(0,init);table(1,P('H',0)*count);table(2,ntab(names))
    table(3,bytes(0x138*count));table(4,P('I',2)) # back-face cull
    table(5,bytes([255,255,255,255]));table(6,bytes([1]))
    # enable, matSrc vertex, lights 0+1, diffuse clamp, attenuation none, ambient register
    table(7,bytes([1,1,3,2,2,0,255,255, 0,1,0,0,2,0,255,255]))
    table(8,bytes([110,110,110,255]));table(10,bytes([0]))
    table(16,bytes([255,255,4,255])) # no texture, COLOR0A0
    table(17,P('hhhh',0,0,0,0));table(18,bytes([255]*4));table(19,bytes([1]))
    # TEV out = raster, alpha = raster alpha. GX_CC_ZERO=15, RASC=10.
    table(20,bytes([255,15,15,15,10,0,0,0,1,0,7,7,7,5,0,0,0,1,0,255]))
    table(21,bytes([0,0,255,255]));table(22,bytes([0,1,2,3]))
    fog=bytearray(44);struct.pack_into('>ffff',fog,4,0,100000,1,100000)
    table(23,fog);table(24,bytes([7,0,0,7,0,255,255,255]))
    table(25,bytes([0,1,0,3]));table(26,bytes([1,3,1,255]));table(27,bytes([1]));table(28,bytes([0]))
    table(29,bytes(4)+P('fff',1,1,1))
    for i,o in enumerate(offsets):struct.pack_into('>I',d,12+4*i,o)
    return section('MAT3',d)

C=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))

def local_matrix(b,pose=False):
    m=b.matrix.copy() if pose else b.matrix_local.copy()
    if b.parent:
        parent=b.parent.matrix if pose else b.parent.matrix_local
        return parent.inverted()@m
    return C@m

def srt(m):
    loc,quat,scale=m.decompose();e=quat.to_euler('XYZ')
    return list(scale),list(e),[x*100 for x in loc]

def bmd(mesh,rig):
    bones=list(rig.data.bones);ids={b.name:i for i,b in enumerate(bones)}
    vertices=mesh.data.vertices;mesh.data.calc_loop_triangles()
    groups={g.index:ids[g.name] for g in mesh.vertex_groups if g.name in ids}
    bind={i:b.matrix_local.inverted() for i,b in enumerate(bones)}
    owner=[]
    for v in vertices:
        w=[g for g in v.groups if g.group in groups and g.weight>.001]
        assert len(w)==1 and abs(w[0].weight-1)<.001,'Exporter supports rigid weights only'
        owner.append(groups[w[0].group])
    pos=[];normal=[];colors=[];indices={};tris=defaultdict(list)
    def rgb(c):return max(0,min(255,round(255*(12.92*c if c<=.0031308 else 1.055*c**(1/2.4)-.055))))
    # Per-corner colour layer (v2 gradients); fall back to flat material colour (v1).
    layer=mesh.data.color_attributes.get('Col')
    for t in mesh.data.loop_triangles:
        bone=owner[t.vertices[0]]
        assert all(owner[v]==bone for v in t.vertices),'Mixed-bone triangle'
        if not layer:
            mat=mesh.data.materials[t.material_index]
            p=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
            flat=tuple(rgb(x) for x in p.inputs['Base Color'].default_value[:3])+(255,)
        tri=[]
        for vi,li in zip(t.vertices,t.loops):
            color=tuple(rgb(x) for x in layer.data[li].color[:3])+(255,) if layer else flat
            key=(vi,color)
            if key not in indices:
                indices[key]=len(pos);v=vertices[vi]
                pos.append(tuple((bind[bone]@v.co)*100))
                normal.append(tuple((bind[bone].to_3x3()@v.normal).normalized()))
                colors.append(color)
            tri.append(indices[key])
        tris[bone].append(tri)
    assert len(pos)<65535
    shape_bones=sorted(tris);shape_ids={b:i for i,b in enumerate(shape_bones)};sections=[]
    inf=bytearray(24);struct.pack_into('>HHIII',inf,8,0,0xffff,len(shape_bones),len(pos),24)
    def node(kind,index):inf.extend(P('HH',kind,index))
    def hierarchy(b):
        bi=ids[b.name];node(0x10,bi);node(1,0)
        if bi in shape_ids:
            si=shape_ids[bi];node(0x11,si);node(1,0);node(0x12,si);node(2,0)
        for child in b.children:hierarchy(child)
        node(2,0)
    for b in bones:
        if not b.parent:hierarchy(b)
    node(0,0);sections.append(section('INF1',inf))
    vtx=bytearray(64)
    fmt=b''.join(P('IIIBxxx',*args) for args in [(9,1,4,0),(10,0,4,0),(11,1,5,0),(255,0,0,0)])
    offsets=[0]*14;offsets[0]=add(vtx,fmt,32)
    offsets[1]=add(vtx,b''.join(P('fff',*v) for v in pos),32)
    offsets[2]=add(vtx,b''.join(P('fff',*v) for v in normal),32)
    offsets[4]=add(vtx,b''.join(bytes(c) for c in colors),32)
    for i,o in enumerate(offsets):struct.pack_into('>I',vtx,8+4*i,o)
    sections.append(section('VTX1',vtx))
    evp=bytearray(28);struct.pack_into('>HH',evp,8,0,0xffff);sections.append(section('EVP1',evp))
    drw=bytearray(20);struct.pack_into('>HH',drw,8,len(bones),0xffff)
    a=add(drw,bytes(len(bones)));b=add(drw,b''.join(P('H',i) for i in range(len(bones))))
    struct.pack_into('>II',drw,12,a,b);sections.append(section('DRW1',drw))
    jnt=bytearray(24);struct.pack_into('>HH',jnt,8,len(bones),0xffff);records=bytearray()
    for b in bones:
        scale,rot,loc=srt(local_matrix(b));r=[round(x*32768/math.pi) for x in rot]
        r=[(x+32768)%65536-32768 for x in r]
        # Bounds deliberately encompass every action's translations, avoiding early culling.
        records.extend(P('HBBfffhhhHffffffffff',0,0,255,*scale,*r,0xffff,*loc,500,-400,-400,-400,400,400,400))
    a=add(jnt,records);b=add(jnt,b''.join(P('H',i) for i in range(len(bones))));c=add(jnt,ntab([b.name for b in bones]))
    struct.pack_into('>III',jnt,12,a,b,c);sections.append(section('JNT1',jnt))
    shp=bytearray(44);struct.pack_into('>HH',shp,8,len(shape_bones),0xffff)
    records=bytearray();draws=bytearray();mtx=bytearray();dl=bytearray()
    for si,bi in enumerate(shape_bones):
        records.extend(P('BBHHHHHfffffff',0,255,1,0,si,si,0xffff,500,-400,-400,-400,400,400,400))
        # GX front faces wind clockwise, Blender's anticlockwise: reverse each triangle,
        # or back-face culling draws the inside of the model.
        align(dl);start=len(dl);flat=[v for tri in tris[bi] for v in (tri[0],tri[2],tri[1])]
        for k in range(0,len(flat),60000):
            sub=flat[k:k+60000];dl.extend(P('BH',0x90,len(sub)))
            for vi in sub:dl.extend(P('HHH',vi,vi,vi))
        align(dl);draws.extend(P('II',len(dl)-start,start));mtx.extend(P('HHI',bi,1,si))
    offsets=[add(shp,records),add(shp,b''.join(P('H',i) for i in range(len(shape_bones)))),0]
    offsets.append(add(shp,b''.join(P('II',*x) for x in [(9,3),(10,3),(11,3),(255,0)])))
    offsets.append(add(shp,b''.join(P('H',i) for i in shape_bones)))
    offsets.extend([add(shp,dl,32),add(shp,mtx),add(shp,draws)])
    for i,o in enumerate(offsets):struct.pack_into('>I',shp,12+4*i,o)
    sections.append(section('SHP1',shp))
    sections.append(mat3(['Barney_'+bones[b].name for b in shape_bones]))
    tex=bytearray(20);struct.pack_into('>HHII',tex,8,0,0xffff,0,20);tex.extend(ntab([]));sections.append(section('TEX1',tex))
    return j3d(b'J3D2bmd3',sections),{'triangles':sum(len(ts) for ts in tris.values()),'vertices':len(pos),'bones':[b.name for b in bones],'shapes':len(shape_bones)}

def bck(rig,action):
    rig.animation_data.action=action
    length=int(action.frame_range[1]-action.frame_range[0]);samples=[[] for b in rig.pose.bones]
    for f in range(length+1):
        bpy.context.scene.frame_set(f+1)
        for i,b in enumerate(rig.pose.bones):samples[i].append(srt(local_matrix(b,True)))
    arrays=[[],[],[]];table=bytearray()
    for joint in samples:
        for axis in range(3):
            for kind in range(3):
                vals=[j[kind][axis] for j in joint]
                if kind==1:
                    for k in range(1,len(vals)):
                        while vals[k]-vals[k-1]>math.pi:vals[k]-=2*math.pi
                        while vals[k]-vals[k-1]<-math.pi:vals[k]+=2*math.pi
                    vals=[round(v*16384/math.pi) for v in vals]
                    assert all(-32768<=v<=32767 for v in vals)
                arr=arrays[kind];idx=len(arr)
                # Rigid child offsets jitter at ~1e-8 between frames; treat those as constants so
                # the key tables stay small (an index past 32767 breaks some readers).
                if max(vals)-min(vals)<(1e-3 if kind==2 else 1e-5 if kind==0 else 0.5):arr.append(vals[0]);count=1
                else:
                    count=len(vals)
                    for f,v in enumerate(vals):arr.extend([f,v,0])
                assert idx<32768,'key table too large'
                table.extend(P('HHH',count,idx,0))
    d=bytearray(36);struct.pack_into('>BBHHHHH',d,8,2 if action.get('loop') else 0,1,length,len(samples),*(len(a) for a in arrays))
    offsets=[add(d,table)]
    for kind,arr in enumerate(arrays):offsets.append(add(d,b''.join(P('h' if kind==1 else 'f',v) for v in arr)))
    struct.pack_into('>IIII',d,20,*offsets)
    return j3d(b'J3D1bck1',[section('ANK1',d)])

def rarc(files):
    # Flat resource archive: ResourceHolder locates model/animations by basename.
    names=['Barney','.','..']+list(files);strings=bytearray();offs={}
    for n in names:offs[n]=len(strings);strings.extend(n.encode()+b'\0')
    def hashname(n):
        h=0
        for b in n.encode():h=(h*3+b)&65535
        return h
    count=len(files)+2;d=bytearray(64)
    nodes=add(d,P('IIHHI',0x524f4f54,offs['Barney'],hashname('Barney'),count,0),32)
    entries=bytearray();payload=bytearray()
    for i,(n,blob) in enumerate(files.items()):
        align(payload);entries.extend(P('HHHHIII',i,hashname(n),0x1100,offs[n],len(payload),len(blob),0));payload.extend(blob)
    for n,idx in [('.',0),('..',0xffffffff)]:entries.extend(P('HHHHIII',0xffff,hashname(n),0x0200,offs[n],idx,0x10,0))
    eo=add(d,entries,32);so=add(d,strings,32);align(d);dataoff=len(d);align(payload);d.extend(payload)
    d[:4]=b'RARC';struct.pack_into('>IIIIIII',d,4,len(d),0x20,dataoff-0x20,len(payload),len(payload),0,0)
    struct.pack_into('>IIIIIIHBBI',d,32,1,nodes-32,count,eo-32,len(strings),so-32,len(files),1,0,0)
    return bytes(d)

def main():
    scene=bpy.context.scene;rig=bpy.data.objects['Barney_Rig'];source=bpy.data.objects['Barney_Mesh']
    rig.matrix_world=Matrix.Identity(4)
    old=rig.animation_data.action;rig.animation_data.action=None
    for p in rig.pose.bones:p.matrix_basis=Matrix.Identity(4)
    scene.frame_set(1)
    mesh=source.copy();mesh.data=source.data.copy();scene.collection.objects.link(mesh);mesh.name='Barney_GameMesh'
    for m in list(mesh.modifiers):mesh.modifiers.remove(m)
    bpy.ops.object.select_all(action='DESELECT');mesh.select_set(True);bpy.context.view_layer.objects.active=mesh
    # build_barney.py already emits a triangulated, game-budget mesh.
    assert all(len(p.vertices)==3 for p in mesh.data.polygons)
    mod=mesh.modifiers.new('Skin','ARMATURE');mod.object=rig
    data,stats=bmd(mesh,rig);files={'Barney.bmd':data}
    actions=[bpy.data.actions[n] for n in ['Wait','Walk','Run','Happy','Talk','Sit','Jump','Blink']]
    for action in actions:files[action.name+'.bck']=bck(rig,action)
    for n,data in files.items():(OUT/'game'/n).write_bytes(data)
    (OUT/'game/Barney.arc').write_bytes(rarc(files))
    # Portable animated copy for viewers and editors; game export uses the same mesh.
    rig.animation_data.action=old;scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT');mesh.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
    rig.animation_data.action=None
    tracks=[]
    for a in actions:
        track=rig.animation_data.nla_tracks.new();track.name=a.name
        track.strips.new(a.name,1,a);tracks.append(track)
    bpy.ops.export_scene.gltf(filepath=str(OUT/'exports/Barney.glb'),export_format='GLB',use_selection=True,use_active_scene=True,export_animations=True,export_animation_mode='NLA_TRACKS',export_frame_range=False,export_force_sampling=True)
    for track in tracks:rig.animation_data.nla_tracks.remove(track)
    rig.matrix_world=Matrix.Identity(4)
    stats['actions']={a.name:{'frames':int(a.frame_range[1]-a.frame_range[0]),'fps':60,'loop':bool(a.get('loop'))} for a in actions}
    stats['format']='Big-endian J3D2bmd3 + J3D1bck1, uncompressed RARC'
    stats['game_tested']=False
    (OUT/'exports/manifest.json').write_text(json.dumps(stats,indent=2)+'\n')
    bpy.data.objects.remove(mesh,do_unlink=True)
    rig.animation_data.action=old;scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Barney.blend'))
    print(json.dumps(stats))

if __name__=='__main__':main()
