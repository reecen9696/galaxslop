"""Barney v3: Super Mario Galaxy style, modelled after the in-game MoonRabbit.

Galaxy NPCs are low-poly, smoothly shaded, rigid per-joint parts whose detail lives in
clean silhouettes and soft colour gradients rather than geometry. Barney follows that:
few large forms per joint, overlapping at ball joints so nothing shows a seam when it
bends, and per-vertex colour (apricot coat, cream muzzle, sun-bleached fringe).

The jumper is the body surface over the torso: there is no fur mesh underneath it, so it
can never clip. The fringe is a scalloped cap sitting proud of the skull, like a wig.

Run headless:
  Blender -b --factory-startup --python build_barney.py
+Z up, -Y forward, 1 Blender metre = 100 game units. Barney stands ~1.1 m (110 units).
"""
import bpy, bmesh, math, json
from pathlib import Path
from mathutils import Vector, Euler, Matrix
from mathutils.bvhtree import BVHTree

OUT = Path(__file__).resolve().parents[1]  # the character folder (this file is in source/)
K = 1/0.574  # metaball radius that gives a lone element a visible half-extent of 1

scene = bpy.context.scene
for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
scene.name = 'Barney • Galaxy'
scene.render.fps = 60
col = bpy.data.collections.new('Barney | export'); scene.collection.children.link(col)

def lin(c): return tuple(x**2.2 for x in c)
def mix(a, b, t): t = max(0, min(1, t)); return tuple(x+(y-x)*t for x, y in zip(a, b))
def smooth(e0, e1, x): t = max(0, min(1, (x-e0)/(e1-e0))); return t*t*(3-2*t)

# Palette, sampled from the photos (sRGB 0-1).
APRICOT   = (0.86, 0.62, 0.35)
APRICOT_D = (0.70, 0.46, 0.23)
GOLD_TIP  = (0.96, 0.81, 0.56)
CREAM     = (0.97, 0.93, 0.84)
EAR       = (0.76, 0.50, 0.25)
SWEATER   = (0.52, 0.09, 0.14)
SWEATER_D = (0.36, 0.05, 0.08)
BLACK     = (0.05, 0.04, 0.04)
IRIS      = (0.28, 0.14, 0.06)
WHITE     = (1.0, 1.0, 1.0)
PINK      = (0.90, 0.42, 0.46)
PINK_D    = (0.72, 0.25, 0.32)
MOUTH     = (0.22, 0.05, 0.07)
TAG       = (1.0, 0.78, 0.22)

parts = []

def finish(me, name, bone, color_fn, faces=None, relax=0):
    """Mesh -> relaxed, decimated, smooth, single-bone part with a corner colour layer."""
    o = bpy.data.objects.new(name, me); col.objects.link(o)
    tris = sum(len(p.vertices)-2 for p in me.polygons)
    if relax:
        m = o.modifiers.new('relax', 'SMOOTH'); m.factor = 0.6; m.iterations = relax
    if faces and tris > faces:
        d = o.modifiers.new('budget', 'DECIMATE'); d.ratio = faces/tris
    if o.modifiers:
        dg = bpy.context.evaluated_depsgraph_get()
        new = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); o.modifiers.clear()
        old = o.data; o.data = new; bpy.data.meshes.remove(old); new.name = name
    me = o.data
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.triangulate(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = True
    attr = me.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    for loop in me.loops:
        v = me.vertices[loop.vertex_index]
        attr.data[loop.index].color = (*lin(color_fn(Vector(v.co), Vector(v.normal))), 1)
    vg = o.vertex_groups.new(name=bone); vg.add(range(len(me.vertices)), 1, 'REPLACE')
    parts.append(o); return o

def blob(name, bone, elements, color_fn, faces, res=0.016, relax=4):
    """elements: (center, half-extents, euler[, stiffness]). Merged into one smooth form."""
    mb = bpy.data.metaballs.new('mb_'+name); mb.resolution = mb.render_resolution = res; mb.threshold = 0.6
    for c, s, r, *rest in elements:
        e = mb.elements.new(); e.type = 'ELLIPSOID'; e.co = c; e.radius = K
        e.size_x, e.size_y, e.size_z = s
        if r: e.rotation = Euler(r).to_quaternion()
        if rest: e.stiffness = rest[0]
    o = bpy.data.objects.new('mb_'+name, mb); scene.collection.objects.link(o)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); me.name = name
    bpy.data.objects.remove(o); bpy.data.metaballs.remove(mb)
    return finish(me, name, bone, color_fn, faces, relax)

def ellipsoid(name, bone, center, half, color, axes=None, seg=14, rings=9):
    """Smooth UV ellipsoid; axes (x, y, z) orient it, e.g. onto a face surface."""
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1)
    m = Matrix.Diagonal((*half, 1))
    if axes: m = Matrix((axes[0], axes[1], axes[2])).transposed().to_4x4() @ m
    bmesh.ops.transform(bm, matrix=Matrix.Translation(center) @ m, verts=bm.verts)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    return finish(me, name, bone, color if callable(color) else (lambda p, n: color))

def tube(name, bone, pts, radius, color, sides=7, closed=True):
    """Round tube through ordered points (ribbed hems, the tag ring)."""
    bm = bmesh.new(); rings = []; n = len(pts)
    for i, p in enumerate(pts):
        t = (pts[(i+1) % n]-pts[i-1]).normalized(); u = t.cross(Vector((0, 0, 1)))
        if u.length < 0.2: u = t.cross(Vector((0, 1, 0)))
        u.normalize(); v = t.cross(u)
        rings.append([bm.verts.new(p+radius*(u*math.cos(2*math.pi*j/sides)+v*math.sin(2*math.pi*j/sides))) for j in range(sides)])
    for i in range(n if closed else n-1):
        a, b = rings[i], rings[(i+1) % n]
        for j in range(sides): bm.faces.new((a[j], b[j], b[(j+1) % sides], a[(j+1) % sides]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    return finish(me, name, bone, color if callable(color) else (lambda p, n: color))

def cap(name, bone, over, center, edge_fn, color_fn, azimuths=40, rows=8, lift=0.012, flare=0.02, lip=0.014):
    """Shell draped over the top of `over` (raycast from `center`), whose lower edge is the
    exact curve edge_fn(azimuth) -> polar angle, flaring out towards it and finishing in a
    rolled lip tucked back onto the surface. Used for the fringe: crisp scallops, real thickness."""
    tree = BVHTree.FromObject(over, bpy.context.evaluated_depsgraph_get()); C = Vector(center)
    def on(d, off):
        hit, nrm, _, _ = tree.ray_cast(C, d); assert hit, name
        return hit + nrm*off
    bm = bmesh.new(); pole = bm.verts.new(on(Vector((0, 0, 1)), lift))
    grid = []
    for i in range(azimuths):
        a = 2*math.pi*i/azimuths; te = edge_fn(a); ring = []
        def d(t): return Vector((math.sin(t)*math.cos(a), math.sin(t)*math.sin(a), math.cos(t)))
        for j in range(1, rows+1):
            ring.append(bm.verts.new(on(d(te*j/rows), lift+(j/rows)**2*flare)))
        edge = on(d(te), lift+flare)
        ring.append(bm.verts.new(edge+Vector((0, 0, -lip))))               # hair hanging past the edge
        ring.append(bm.verts.new(on(d(te+math.radians(5)), 0.0)))          # tucked back onto the skull
        grid.append(ring)
    for i in range(azimuths):
        a, b = grid[i], grid[(i+1) % azimuths]
        bm.faces.new((pole, a[0], b[0]))
        for j in range(rows+1): bm.faces.new((a[j], a[j+1], b[j+1], b[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    return finish(me, name, bone, color_fn)

def boundary_loop(bm):
    edges = [e for e in bm.edges if e.is_boundary]; start = edges[0].verts[0]; loop = [start]; prev = None; cur = start
    while True:
        nxt = next(e.other_vert(cur) for e in cur.link_edges if e.is_boundary and e.other_vert(cur) is not prev)
        if nxt is start: return [v.co.copy() for v in loop]
        prev, cur = cur, nxt; loop.append(cur)

def cut(obj, plane_co, plane_no, color_fn):
    """Keep the side of the plane the normal points away from; returns the cut edge loop."""
    bm = bmesh.new(); bm.from_mesh(obj.data)
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:]+bm.edges[:]+bm.faces[:], plane_co=plane_co, plane_no=plane_no, clear_outer=True)
    loop = boundary_loop(bm)
    bmesh.ops.triangulate(bm, faces=bm.faces); bm.to_mesh(obj.data); bm.free()
    attr = obj.data.color_attributes['Col']
    for l in obj.data.loops:
        v = obj.data.vertices[l.vertex_index]; attr.data[l.index].color = (*lin(color_fn(v.co, v.normal)), 1)
    return loop

def fur(base=APRICOT, dark=APRICOT_D, tip=GOLD_TIP):
    """Coat shading baked like Galaxy vertex colour: darker underneath, bleached on top."""
    def f(p, n):
        c = mix(dark, base, smooth(-0.6, 0.3, n.z))
        return mix(c, tip, 0.45*smooth(0.35, 1.0, n.z))
    return f

def surface(obj, origin, direction):
    tree = BVHTree.FromObject(obj, bpy.context.evaluated_depsgraph_get())
    hit, normal, _, _ = tree.ray_cast(Vector(origin), Vector(direction).normalized())
    assert hit, f'no surface for {obj.name}'
    return hit, normal

def frame(normal, up=Vector((0, 0, 1))):
    z = normal.normalized(); x = up.cross(z).normalized(); y = z.cross(x)
    return x, y, z

# ---------------------------------------------------------------- head
H = Vector((0, -0.40, 0.81)); SK = (0.245, 0.23, 0.22)
MUZZLE = Vector((0, -0.70, 0.73)); LIP = 0.648   # mouth line height
head_el = [(H, SK, None),
           (H+Vector((0, -0.03, 0.04)), (0.22, 0.20, 0.20), None),
           ((0.0, -0.555, 0.84), (0.215, 0.05, 0.11), None),          # flat face plate (brow and eyes)
           ((0.0, -0.66, 0.74), (0.115, 0.17, 0.08), None),           # long muzzle
           ((0.0, -0.68, 0.685), (0.10, 0.16, 0.04), None),           # lower muzzle, bottom = lip line
           ((0.0, -0.73, 0.76), (0.08, 0.11, 0.06), None)]            # bridge of the nose (ends before the eyes)
for s in (-1, 1):
    head_el += [((s*0.095, -0.60, 0.73), (0.10, 0.11, 0.082), None),   # soft cheeks, like the rabbit's
                ((s*0.19, -0.50, 0.71), (0.06, 0.07, 0.10), (0, s*0.35, 0))]  # whiskery side fluff
def head_color(p, n):
    c = fur()(p, n)
    c = mix(c, CREAM, smooth(0.25, 0.14, (p-MUZZLE).length) if p.y < -0.5 else 0)
    c = mix(c, CREAM, smooth(-0.56, -0.64, p.y)*smooth(0.76, 0.68, p.z))
    return mix(c, APRICOT_D, 0.4*smooth(0.90, 0.96, p.z))   # shadow under the fringe
head = blob('Head', 'Head', head_el, head_color, 1500, 0.013, 5)

# Fringe: a scalloped cap over the skull. Low over the brow with soft points, high over the
# ears, down to the nape at the back. This is Barney's mop, drawn the way Galaxy would.
def fringe_edge(a):
    front = smooth(0.0, 1.0, max(0, -math.sin(a)))         # 1 at the front (a=-90°)
    back = smooth(0.0, 1.0, max(0, math.sin(a)))**1.3      # 1 at the nape
    base = math.radians(45 + 9*front + 68*back)
    wave = math.radians(3.5)*math.sin(a*9)                 # shaggy all round
    pts = math.radians(6)*max(0, math.sin(a*3.5+1.2))*front   # softer points over the brow
    return base + wave + pts
def fringe_color(p, n):
    c = mix(APRICOT, GOLD_TIP, 0.15+0.45*smooth(0.1, 1.0, n.z))
    return mix(APRICOT_D, c, smooth(-0.9, 0.3, n.z))
cap('Fringe', 'Head', head, H+Vector((0, -0.01, 0.0)), fringe_edge, fringe_color, 48, 9, 0.005, 0.012, 0.007)

# Eyes: layered decals on the face surface (Galaxy eyes read as glossy painted ovals).
EYE = {}
for side, s in (('L', 1), ('R', -1)):
    p, n = surface(head, (s*0.135, -1.2, 0.85), (0, 1, 0))
    x, y, z = frame(n.lerp(Vector((0, -1, 0)), 0.5))
    def at(depth, dx=0, dy=0): return p + z*depth + x*dx + y*dy
    EYE[side] = p
    ellipsoid('EyeRim'+side, 'Eye'+side, at(-0.0145), (0.064, 0.080, 0.034), (0.25, 0.13, 0.06), (x, y, z), 24, 12)
    ellipsoid('EyeWhite'+side, 'Eye'+side, at(-0.013), (0.060, 0.076, 0.034), WHITE, (x, y, z), 24, 12)
    ellipsoid('Iris'+side, 'Eye'+side, at(-0.010, -s*0.003, -0.005), (0.050, 0.064, 0.033), IRIS, (x, y, z), 20, 10)
    ellipsoid('Pupil'+side, 'Eye'+side, at(-0.007, -s*0.004, -0.007), (0.036, 0.046, 0.032), BLACK, (x, y, z), 18, 9)
    ellipsoid('Glint'+side, 'Eye'+side, at(0.021, -0.012, 0.022), (0.014, 0.017, 0.005), WHITE, (x, y, z), 10, 6)
    ellipsoid('GlintSmall'+side, 'Eye'+side, at(0.020, 0.013, -0.021), (0.006, 0.007, 0.004), WHITE, (x, y, z), 8, 5)

# Nose: rounded black triangle with a highlight, sat on the bridge.
p, n = surface(head, (0, -1.2, 0.785), (0, 1, 0))
x, y, z = frame(Vector((0, -1, 0.3)))
def nose_color(q, m): return mix(BLACK, (0.30, 0.27, 0.27), 0.6*smooth(0.4, 1.0, m.z))
nose = ellipsoid('Nose', 'Head', p + z*0.014, (0.06, 0.044, 0.04), nose_color, (x, y, z), 16, 10)
for v in nose.data.vertices:
    if v.co.z < p.z: v.co.x *= 0.72 + 0.28*smooth(p.z-0.04, p.z, v.co.z)
ellipsoid('NoseShine', 'Head', p + z*0.048 + y*0.018 - x*0.014, (0.016, 0.009, 0.006), (0.85, 0.85, 0.9), (x, y, z), 8, 5)

# Mouth: a dark lip line where muzzle meets chin, a chin on the jaw that drops open, and a
# tongue on its own joint that hangs out and swings when he runs.
ellipsoid('MouthInside', 'Head', (0, -0.68, LIP-0.002), (0.088, 0.115, 0.011), MOUTH, None, 18, 8)
ellipsoid('MouthFloor', 'Jaw', (0, -0.67, LIP+0.002), (0.082, 0.13, 0.010), MOUTH, None, 18, 8)
blob('Chin', 'Jaw', [((0, -0.65, 0.614), (0.105, 0.165, 0.04), None),
                     ((0, -0.52, 0.62), (0.12, 0.08, 0.05), None),
                     ((0, -0.70, 0.578), (0.06, 0.065, 0.05), (0.3, 0, 0))]
     + [((x, -0.70+abs(x)*0.5, 0.56), (0.03, 0.035, 0.05), (0.2, t, 0)) for x, t in ((-0.065, -0.4), (0.0, 0.0), (0.065, 0.4))],
     lambda p, n: mix((0.86, 0.78, 0.66), CREAM, smooth(-0.7, 0.4, n.z)), 320, 0.011, 3)
def tongue_color(p, n): return mix(PINK_D, PINK, smooth(-0.5, 0.6, n.z))
blob('Tongue', 'Tongue', [((0, -0.66, 0.66), (0.034, 0.07, 0.012), None),
                          ((0, -0.77, 0.657), (0.034, 0.05, 0.012), (0.1, 0, 0)),
                          ((0, -0.83, 0.63), (0.032, 0.04, 0.012), (0.7, 0, 0)),
                          ((0, -0.855, 0.59), (0.028, 0.03, 0.012), (1.1, 0, 0))], tongue_color, 220, 0.008, 3)

# ---------------------------------------------------------------- ears
for side, s in (('L', 1), ('R', -1)):
    el = [((s*0.23, -0.38, 0.86), (0.055, 0.10, 0.06), (0, s*0.55, 0)),
          ((s*0.27, -0.39, 0.77), (0.052, 0.12, 0.13), (0, s*0.2, 0)),
          ((s*0.285, -0.39, 0.65), (0.05, 0.125, 0.10), (0, s*0.1, 0))]
    for dy, z, h in ((-0.09, 0.57, 0.06), (-0.02, 0.55, 0.075), (0.05, 0.56, 0.07), (0.11, 0.59, 0.055)):
        el.append(((s*0.29, -0.39+dy, z), (0.045, 0.045, h), (0, s*0.1, 0)))
    blob('Ear'+side, 'Ear'+side, el,
         lambda p, n: mix(fur(EAR, (0.56, 0.33, 0.15), APRICOT)(p, n), GOLD_TIP, 0.35*smooth(0.62, 0.52, p.z)), 600, 0.011, 5)

# ---------------------------------------------------------------- body
# Long, stocky barrel. The jumper replaces the front of it entirely; fur shows at the rump.
torso_el = [((0, 0.155, 0.47), (0.23, 0.555, 0.20), None),
            ((0, -0.22, 0.52), (0.215, 0.17, 0.205), None),
            ((0, 0.58, 0.47), (0.215, 0.18, 0.19), None)]
HEM_Y = 0.24
body = blob('Rump', 'Spine', torso_el, fur(), 900, 0.018, 4)
cut(body, (0, HEM_Y-0.03, 0), (0, -1, 0), fur())
blob('Neck', 'Chest', [((0, -0.34, 0.63), (0.15, 0.12, 0.14), None)], fur(), 160, 0.02, 3)

def jumper_color(p, n): return mix(SWEATER_D, SWEATER, smooth(-0.7, 0.4, n.z))
jumper = blob('Jumper', 'Spine', [(c, tuple(x+0.02 for x in sz), r) for c, sz, r in torso_el]
              + [((0, -0.31, 0.60), (0.165, 0.13, 0.16), None)], jumper_color, 900, 0.018, 4)
hem = cut(jumper, (0, HEM_Y, 0), (0, 1, 0), jumper_color)
hem = hem[::max(1, len(hem)//30)]
tube('JumperHem', 'Spine', hem, 0.02, lambda p, n: mix(SWEATER_D, SWEATER, 0.5*smooth(-0.5, 0.8, n.z)), 7)
# Roll neck: a soft ring where the head meets the jumper.
blob('RollNeck', 'Chest', [((0, -0.38, 0.64), (0.19, 0.075, 0.17), (-0.5, 0, 0))]
     + [((0, -0.38, 0.64), (0.14, 0.11, 0.12), (-0.5, 0, 0))], jumper_color, 300, 0.012, 3)
# Bone-shaped name tag (his real tag is a little bone) on a ring under the roll neck.
blob('Tag', 'Chest', [((0, -0.515, 0.52), (0.04, 0.012, 0.016), None)]
     + [((sx*0.04, -0.515, 0.52+sz*0.012), (0.017, 0.012, 0.017), None) for sx in (-1, 1) for sz in (-1, 1)],
     lambda p, n: mix((0.78, 0.55, 0.12), TAG, smooth(-0.3, 0.8, n.z-n.y*0.3)), 120, 0.005, 0)
tube('TagRing', 'Chest', [Vector((0.016*math.cos(a), -0.515, 0.555+0.016*math.sin(a))) for a in [2*math.pi*i/10 for i in range(10)]], 0.004, TAG, 4)

# ---------------------------------------------------------------- legs
# Two parts per leg: an upper that ends in a ball at the knee, and a foot that starts as a
# socket around that ball, so the joint hides itself when it bends.
LEGS = {}
KNEE = 0.27
def foot_color(p, n): return mix(fur()(p, n), mix(CREAM, GOLD_TIP, 0.3), smooth(0.09, 0.03, p.z))
for side, s in (('L', 1), ('R', -1)):
    for label, y, rear in (('Front', -0.20, False), ('Rear', 0.63, True)):
        n = label+side; x = s*(0.15 if not rear else 0.16); LEGS[n] = (x, y)
        upper = ((x*0.74, y+0.02, 0.41), (0.065, 0.135, 0.15), None) if rear else ((x, y, 0.42), (0.078, 0.078, 0.15), None)
        blob(n+'Leg', n, [upper, ((x, y, 0.22), (0.068, 0.07, 0.12), None),
                          ((x, y-0.025, 0.05), (0.08, 0.10, 0.05), None)]
             + [((x+dx, y-0.105, 0.04), (0.027, 0.03, 0.028), None) for dx in (-0.042, 0.0, 0.042)],
             foot_color, 460, 0.013, 5)

# ---------------------------------------------------------------- tail
# Base tapers up into a ball at the tip joint; the plume starts as a socket around it.
TJ = Vector((0, 0.83, 0.71))
blob('Tail', 'Tail', [((0, 0.75, 0.56), (0.055, 0.075, 0.06), (0.6, 0, 0)),
                      ((0, 0.80, 0.64), (0.05, 0.05, 0.065), (0.35, 0, 0)),
                      ((0, 0.83, 0.74), (0.058, 0.058, 0.08), (0.1, 0, 0)),
                      ((0, 0.84, 0.83), (0.062, 0.062, 0.09), (0.0, 0, 0)),
                      ((0, 0.83, 0.91), (0.045, 0.045, 0.065), (-0.25, 0, 0)),
                      ((0, 0.80, 0.96), (0.028, 0.028, 0.04), (-0.6, 0, 0))],
     lambda p, n: mix(fur()(p, n), GOLD_TIP, 0.45*smooth(0.8, 0.96, p.z)), 420, 0.012, 5)

# ---------------------------------------------------------------- skeleton
arm = bpy.data.armatures.new('Barney skeleton'); rig = bpy.data.objects.new('Barney_Rig', arm); col.objects.link(rig)
bpy.context.view_layer.objects.active = rig; rig.show_in_front = True
bpy.ops.object.mode_set(mode='EDIT')
def bone(name, head, tail, parent=None):
    b = arm.edit_bones.new(name); b.head = head; b.tail = tail
    if parent: b.parent = arm.edit_bones[parent]
bone('Root', (0, 0, 0), (0, 0, 0.12))
bone('Spine', (0, 0.58, 0.47), (0, -0.15, 0.50), 'Root')
bone('Chest', (0, -0.15, 0.50), (0, -0.32, 0.62), 'Spine')
bone('Head', (0, -0.32, 0.62), (0, -0.42, 0.95), 'Chest')
bone('Jaw', (0, -0.55, 0.66), (0, -0.68, 0.62), 'Head')
bone('Tongue', (0, -0.62, 0.66), (0, -0.84, 0.60), 'Jaw')
for side, s in (('L', 1), ('R', -1)):
    bone('Ear'+side, (s*0.22, -0.38, 0.90), (s*0.29, -0.39, 0.58), 'Head')
    # Local Y follows world Z, so scaling local Y squashes the eye shut: a real skeletal blink.
    c = EYE[side]; bone('Eye'+side, c, c+Vector((0, 0, 0.07)), 'Head')
for n, (x, y) in LEGS.items():
    bone(n, (x, y, 0.50), (x, y, KNEE), 'Chest' if n.startswith('Front') else 'Spine')
    bone(n+'Shin', (x, y, KNEE), (x, y, 0.07), n)
    bone(n+'Paw', (x, y, 0.07), (x, y-0.10, 0.04), n+'Shin')
bone('Tail', (0, 0.71, 0.52), TJ, 'Spine')
bone('TailTip', TJ, (0, 0.81, 0.95), 'Tail')
bpy.ops.object.mode_set(mode='OBJECT')

# One mesh, one material that previews the vertex colours; every vertex rigid to one joint.
mat = bpy.data.materials.new('Barney_VertexColour'); mat.use_nodes = True
nt = mat.node_tree; bsdf = nt.nodes['Principled BSDF']
attr_node = nt.nodes.new('ShaderNodeVertexColor'); attr_node.layer_name = 'Col'
nt.links.new(attr_node.outputs['Color'], bsdf.inputs['Base Color'])
bsdf.inputs['Roughness'].default_value = 0.55
for o in parts: o.data.materials.append(mat)
with bpy.context.temp_override(active_object=parts[0], selected_editable_objects=parts, object=parts[0]):
    bpy.ops.object.join()
mesh = parts[0]; mesh.name = mesh.data.name = 'Barney_Mesh'
mesh.parent = rig
mod = mesh.modifiers.new('Barney skin', 'ARMATURE'); mod.object = rig
rig['forward'] = '-Y'; rig['game_units_per_meter'] = 100
rig['animation_notes'] = '60 fps; loops duplicate the opening pose at the final frame. Eye bones squash on local Y for blinks.'

# ---------------------------------------------------------------- actions
U = 0.45  # translation scale relative to the v1 rig (Barney is game-sized)
def animate(name, length, loop=True):
    act = bpy.data.actions.new(name); act.use_fake_user = True
    rig.animation_data_create(); rig.animation_data.action = act
    act['loop'] = loop; act['fps'] = 60
    for f in range(length+1):
        t = f/length; phase = t*2*math.pi
        for pb in rig.pose.bones:
            pb.rotation_mode = 'XYZ'; pb.location = (0, 0, 0); pb.rotation_euler = (0, 0, 0); pb.scale = (1, 1, 1)
        def rot(b, x=0, y=0, z=0): rig.pose.bones[b].rotation_euler = (x, y, z)
        breathing = .5-.5*math.cos(phase)
        rig.pose.bones['Spine'].scale = (1+.02*breathing, 1, 1+.02*breathing)
        rot('Head', .035*math.sin(phase), .025*math.sin(phase), .05*math.sin(phase))
        rot('Tail', 0, .30*math.sin(phase*3), .22*math.sin(phase*3))
        rot('Jaw', .04)                                   # resting: lips just parted, tongue showing
        rot('Tongue', .05*math.sin(phase*6), 0, .04*math.sin(phase*6+1))   # gentle pant
        blink = 0
        if name in ('Wait', 'Talk', 'Happy', 'Sit'):
            for at in (.32, .78) if name == 'Wait' else (.6,):
                blink = max(blink, 1-abs(t-at)*length/5)
        for side, s in (('L', 1), ('R', -1)):
            rig.pose.bones['Eye'+side].scale.y = 1-.92*max(0, blink)
            rot('Ear'+side, .05*math.sin(phase+.3), s*.04*math.sin(phase+.7), s*.05*math.sin(phase))
        if name in ('Walk', 'Run'):
            run = name == 'Run'; amp = .7 if run else .38
            rig.pose.bones['Root'].location.z = (.09 if run else .035)*U*(1-math.cos(phase*2))
            if run: rot('Spine', .06*math.sin(phase*2))
            for side, s in (('L', 1), ('R', -1)):
                for label, q in (('Front', 1), ('Rear', -1)):
                    n = label+side; a = phase+(0 if s*q > 0 else math.pi)
                    rot(n, amp*math.sin(a))
                # Ears flop behind the motion.
                rot('Ear'+side, .22*math.sin(phase*2-.8) - (.25 if run else 0), s*.1*math.cos(phase*2), s*.08*math.cos(phase*2))
            rot('Head', .06*math.sin(phase*2+.4))
            rot('Jaw', .22 if run else .10)
            # Tongue lolls out and swings side to side with the stride, flapping with each bounce.
            rot('Tongue', (-.15 if run else -.05) + .25*math.sin(phase*2+.5), 0, (.55 if run else .2)*math.sin(phase))
            rot('Tail', -.2 if run else 0, .35*math.sin(phase*2), .1*math.sin(phase*2))
        elif name == 'Happy':
            rig.pose.bones['Root'].location.z = .14*U*(1-math.cos(phase*2))
            rot('Head', -.08+.10*math.sin(phase*2), 0, .12*math.sin(phase))
            rot('Jaw', .28+.10*math.sin(phase*2))
            rot('Tongue', -.1+.2*math.sin(phase*2+1), 0, .3*math.sin(phase*2))
            rot('Tail', 0, .6*math.sin(phase*6), .25*math.sin(phase*6))
            for side, s in (('L', 1), ('R', -1)): rot('Ear'+side, .2*math.sin(phase*2), 0, s*.15*math.sin(phase*2))
        elif name == 'Talk':
            # Barks: two snaps of the jaw with a head toss, then a growly rumble.
            bark = max(0, math.sin(phase*4)) if t < .5 else .25*abs(math.sin(phase*14))
            rot('Jaw', .06+.36*bark); rot('Head', -.12*bark+.05*math.sin(phase), 0, .1*math.sin(phase))
            rot('Tongue', .08*bark, 0, 0)
            rig.pose.bones['Root'].location.z = .03*U*bark
        elif name == 'Sit':
            rig.pose.bones['Root'].location.z = -.10*U
            rot('Spine', -.16); rot('Head', .12)
            for side in 'LR': rot('Rear'+side, -.6); rot('Front'+side, .25)
        elif name == 'Jump':
            h = math.sin(math.pi*t)
            rig.pose.bones['Root'].location.z = .62*U*h
            for side in 'LR': rot('Front'+side, -.6*h); rot('Rear'+side, .5*h); rot('Ear'+side, -.35*h)
            rot('Jaw', .15*h); rot('Tongue', -.3*h, 0, 0)
        elif name == 'Blink':
            blink = max(0, 1-abs(t-.5)/.4)
            for side in 'LR': rig.pose.bones['Eye'+side].scale.y = 1-.92*blink
        for pb in rig.pose.bones:
            for ch in ('location', 'rotation_euler', 'scale'): pb.keyframe_insert(ch, frame=f+1, group=pb.name)
    return act

actions = {n: animate(n, l, lp) for n, l, lp in [('Wait', 180, True), ('Walk', 48, True), ('Run', 28, True),
           ('Happy', 90, True), ('Talk', 120, True), ('Sit', 180, True), ('Jump', 60, False), ('Blink', 12, False)]}
rig.animation_data.action = actions['Wait']
scene.frame_start = 1; scene.frame_end = 181; scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Barney.blend'))
print('BUILD', json.dumps({'triangles': len(mesh.data.polygons), 'vertices': len(mesh.data.vertices),
                           'bones': len(arm.bones), 'actions': list(actions),
                           'height_m': round(mesh.dimensions.z, 3), 'length_m': round(mesh.dimensions.y, 3)}))
