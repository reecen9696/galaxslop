"""Independent parser check with J3DBlend, without installing an add-on."""
import sys, bpy, json
from pathlib import Path
import os
CHAR=Path(__file__).resolve().parents[1]  # the character folder
# J3DBlend (GPL-3, github.com/ryanbevins/J3DBlend) is cloned beside this folder, or J3DBLEND=<dir>.
sys.path.insert(0,os.environ.get('J3DBLEND',str(CHAR.parent/'tools')))
import J3DBlend
J3DBlend.register()
# This asset has no textures. Skip the importer's platform-specific external decoder.
J3DBlend.common.SubProcCall=lambda *a,**kw:None
bpy.ops.import_mesh.bmd(filepath=str(CHAR/'game/Barney.bmd'),files=[{'name':'Barney.bmd'}],import_anims=False,frc_cr_bn=True,use_nodes=True)
print('IMPORTED',[(o.name,o.type,len(o.data.polygons) if o.type=='MESH' else len(o.data.bones) if o.type=='ARMATURE' else 0) for o in bpy.context.scene.objects])
bpy.ops.wm.save_as_mainfile(filepath=str(CHAR/'exports/Barney_BMD_roundtrip.blend'))
