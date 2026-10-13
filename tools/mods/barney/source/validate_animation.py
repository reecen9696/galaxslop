import bpy, sys, json, struct, math
from pathlib import Path
from mathutils import Matrix
import os
CHAR=Path(__file__).resolve().parents[1]  # the character folder
sys.path.insert(0,os.environ.get('J3DBLEND',str(CHAR.parent/'tools')))  # see check_import.py
from J3DBlend.Bck import Bck_in
rig=bpy.data.objects['Barney_Rig'];scene=bpy.context.scene
C=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
report={};maximum=0
for name in ['Wait','Walk','Run','Happy','Talk','Sit','Jump','Blink']:
    a=bpy.data.actions[name];rig.animation_data.action=a
    reader=Bck_in();reader.LoadBck(str(CHAR/'game'/f'{name}.bck'),len(rig.data.bones))
    err=0
    for f in range(reader.animationLength+1):
        scene.frame_set(f+1)
        for bone,channels in zip(rig.pose.bones,reader.anims):
            m=bone.parent.matrix.inverted()@bone.matrix if bone.parent else C@bone.matrix
            loc,quat,scale=m.decompose();rotation=quat.to_euler('XYZ')
            for prefix,values in [('scales',scale),('rotations',rotation),('translations',loc*100)]:
                for axis,value in zip('XYZ',values):
                    keys=getattr(channels,prefix+axis)
                    decoded=keys[0].value if len(keys)==1 else next(k.value for k in keys if k.time==f)
                    delta=abs(decoded-value)
                    if prefix=='rotations':delta=abs((decoded-value+math.pi)%(2*math.pi)-math.pi)
                    err=max(err,delta)
                    assert delta < (0.00011 if prefix=='rotations' else 0.0001),(name,f,bone.name,prefix,axis,delta)
    report[name]={'frames_checked':reader.animationLength+1,'joint_channels_checked':len(rig.data.bones)*9,'max_numeric_error':err}
    maximum=max(maximum,err)
rig.animation_data.action=bpy.data.actions['Wait'];scene.frame_set(1)
(CHAR/'exports/animation_validation.json').write_text(json.dumps(report,indent=2))
print('PASS: all eight BCK files decoded with independent parser; every channel on every frame matches Blender. Max error:',maximum)

