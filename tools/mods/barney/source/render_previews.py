"""Render Barney previews in a Galaxy-like soft studio (does not modify the .blend).
  Blender -b character/Barney.blend --python render_previews.py -- [Action:frame ...]
"""
import bpy, sys
from pathlib import Path
from mathutils import Vector

OUT = Path(__file__).resolve().parents[1]/'previews'; OUT.mkdir(exist_ok=True)
args = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
sc = bpy.context.scene; rig = bpy.data.objects['Barney_Rig']
w = bpy.data.worlds.new('studio'); w.use_nodes = True; sc.world = w
w.node_tree.nodes['Background'].inputs[0].default_value = (0.55, 0.62, 0.78, 1)
w.node_tree.nodes['Background'].inputs[1].default_value = 0.9
def light(rot, energy, color):
    d = bpy.data.lights.new('l', 'SUN'); d.energy = energy; d.color = color; d.angle = 0.3
    o = bpy.data.objects.new('l', d); sc.collection.objects.link(o); o.rotation_euler = rot
light((0.75, 0.15, -0.6), 3.2, (1, 0.95, 0.88)); light((1.2, 0, 2.4), 1.0, (0.75, 0.82, 1))
bpy.ops.mesh.primitive_circle_add(vertices=64, radius=1.4, fill_type='NGON', location=(0, 0, -0.002))
floor = bpy.context.object; m = bpy.data.materials.new('floor'); m.use_nodes = True
m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.33, 0.55, 0.22, 1)
floor.data.materials.append(m)
d = bpy.data.cameras.new('c'); cam = bpy.data.objects.new('c', d); sc.collection.objects.link(cam); sc.camera = cam
d.lens = 70
sc.view_settings.view_transform = 'Standard'
sc.render.engine = 'BLENDER_EEVEE'; sc.render.resolution_x = sc.render.resolution_y = 900
sc.eevee.use_shadows = True if hasattr(sc.eevee, 'use_shadows') else None
target = Vector((0, 0.1, 0.55))
def shot(name, direction, dist=4.9):
    cam.location = target + Vector(direction).normalized()*dist
    cam.rotation_euler = (target-cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = str(OUT/f'{name}.png'); bpy.ops.render.render(write_still=True)
if not args:
    rig.animation_data.action = bpy.data.actions['Wait']; sc.frame_set(1)
    shot('Barney_portrait', (0.75, -1, 0.35)); shot('Barney_front', (0, -1, 0.15)); shot('Barney_side', (1, 0, 0.12))
    shot('Barney_back', (-0.6, 1, 0.4))
for a in args:
    name, frame = a.split(':'); rig.animation_data.action = bpy.data.actions[name]; sc.frame_set(int(frame))
    shot(f'{name}_{int(frame):03d}', (0.9, -1, 0.3))
