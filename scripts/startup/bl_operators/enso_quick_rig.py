# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō Quick Rig: put a few markers on a character, get a rig, drop animations on it.

  1. Place Markers   chin, shoulder, elbow, wrist, groin, knee and ankle markers are guessed
                     from the mesh; move the ones that are off (the right side mirrors the left).
  2. Build Rig       a clean body armature is fitted to the markers and the mesh is bound to
                     it with automatic weights.
  3. Animations      idle, walk, run, ninja run, crouch, jump, punch, kick and swim are written
                     as actions for that rig. The same clips can be written for any other
                     armature whose bones are named like Mixamo, Rigify, VRM or VRoid rigs, so
                     no retargeting step is needed.

The character is expected to stand upright, facing -Y (Blender's front view), in a T or A pose.
All clips are made by Mix 3D Design in code: they play in place and loop where it makes sense.
"""

import math

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty
from bpy.types import Operator, Panel
from mathutils import Quaternion, Vector

def _pack():
    """These tools belong to the Ensō NPR Pack: they stay hidden until a pack is installed."""
    try:
        from . import enso_dlc
        return enso_dlc.has_pack()
    except Exception:
        return False


MARKER_COLL = "Ensō Rig Markers"
MARKERS = ("chin", "groin", "shoulder.L", "elbow.L", "wrist.L", "knee.L", "ankle.L")
X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
D = math.radians

# Bone names of common rigs for each Ensō body part (compared without case, spaces, _ . : -).
ALIASES = {
    "hips": ("hips", "mixamorighips", "pelvis", "jbipchips", "root", "spine"),
    "spine": ("spine", "mixamorigspine", "jbipcspine", "spine001", "spine1"),
    "chest": ("chest", "mixamorigspine2", "mixamorigspine1", "jbipcchest", "upperchest", "spine003", "spine2"),
    "neck": ("neck", "mixamorigneck", "jbipcneck", "spine004"),
    "head": ("head", "mixamorighead", "jbipchead", "spine006"),
    "shoulder.L": ("shoulderl", "mixamorigleftshoulder", "leftshoulder", "jbiplshoulder", "claviclel"),
    "upper_arm.L": ("upperarml", "mixamorigleftarm", "leftupperarm", "leftarm", "jbiplupperarm"),
    "forearm.L": ("forearml", "mixamorigleftforearm", "leftlowerarm", "leftforearm", "jbipllowerarm", "lowerarml"),
    "hand.L": ("handl", "mixamoriglefthand", "lefthand", "jbiplhand"),
    "thigh.L": ("thighl", "mixamorigleftupleg", "leftupperleg", "leftupleg", "jbiplupperleg", "upperlegl"),
    "shin.L": ("shinl", "mixamorigleftleg", "leftlowerleg", "leftleg", "jbipllowerleg", "lowerlegl", "calfl"),
    "foot.L": ("footl", "mixamorigleftfoot", "leftfoot", "jbiplfoot"),
    "toe.L": ("toel", "mixamoriglefttoebase", "lefttoes", "lefttoebase", "jbipltoebase", "toesl"),
}


def _norm(name):
    return "".join(c for c in name.lower() if c.isalnum())


def _mirror_alias(a):
    for l, r in (("left", "right"), ("jbipl", "jbipr")):
        if l in a:
            return a.replace(l, r)
    return a[:-1] + "r" if a.endswith("l") else a


def bone_map(arm):
    """Ensō body part -> bone name for this armature. Missing parts are left out."""
    names = {_norm(b.name): b.name for b in arm.data.bones}
    out = {}
    for key, aliases in ALIASES.items():
        for a in aliases:
            if a in names and names[a] not in out.values():
                out[key] = names[a]
                break
        if key.endswith(".L"):
            for a in aliases:
                ra = _mirror_alias(a)
                if ra in names and names[ra] not in out.values():
                    out[key[:-2] + ".R"] = names[ra]
                    break
    return out


# --------------------------------------------------------------------------------- markers

def _mesh_points(ob):
    mw = ob.matrix_world
    return [mw @ v.co for v in ob.data.vertices]


def _local_center(pts, p, radius):
    near = [q for q in pts if (q - p).length < radius]
    if len(near) < 4:
        return p
    c = Vector((0, 0, 0))
    for q in near:
        c += q
    return c / len(near)


def guess_markers(ob):
    """Marker positions (world) guessed from the mesh: standing, facing -Y, T or A pose."""
    pts = _mesh_points(ob)
    zmin = min(p.z for p in pts); zmax = max(p.z for p in pts)
    H = max(zmax - zmin, 1e-4)
    xs = sorted(p.x for p in pts)
    cx = 0.5 * (xs[0] + xs[-1])
    body = [p for p in pts if abs(p.x - cx) < 0.12 * H]
    cy = sum(p.y for p in body) / max(len(body), 1)

    def at(x, z, r=0.05):
        return _local_center(pts, Vector((x, cy, z)), r * H)

    # The hand is the part of the mesh furthest to the character's left (+X).
    far = max(pts, key=lambda p: p.x)
    tips = [p for p in pts if p.x > far.x - 0.04 * H]
    tip = sum(tips, Vector((0, 0, 0))) / len(tips)
    z_sh = zmin + 0.815 * H
    shoulder = at(cx + 0.105 * H, z_sh, 0.045)
    shoulder.x = cx + 0.105 * H
    arm = tip - shoulder
    wrist = _local_center(pts, shoulder + arm * 0.80, 0.04 * H)
    elbow = _local_center(pts, shoulder + arm * 0.44, 0.045 * H)
    m = {
        "chin": Vector((cx, cy - 0.055 * H, zmin + 0.868 * H)),
        "groin": Vector((cx, cy, zmin + 0.485 * H)),
        "shoulder.L": shoulder,
        "elbow.L": elbow,
        "wrist.L": wrist,
        "knee.L": at(cx + 0.055 * H, zmin + 0.27 * H, 0.05),
        "ankle.L": at(cx + 0.06 * H, zmin + 0.05 * H, 0.04),
    }
    front = min((p.y for p in pts if abs(p.z - m["chin"].z) < 0.02 * H and abs(p.x - cx) < 0.04 * H), default=m["chin"].y)
    m["chin"].y = front
    return m, H, cx


def _marker_collection(scene):
    coll = bpy.data.collections.get(MARKER_COLL)
    if coll is None:
        coll = bpy.data.collections.new(MARKER_COLL)
    if coll.name not in scene.collection.children:
        try:
            scene.collection.children.link(coll)
        except RuntimeError:
            pass
    return coll


def marker_name(key):
    return "Ensō Marker: " + key


class OBJECT_OT_enso_rig_markers(Operator):
    """Put rig markers on the selected character. They are guessed from the mesh: """ \
        """move the ones that are off, then press Build Rig. The right side mirrors the left"""
    bl_idname = "object.enso_rig_markers"
    bl_label = "Place Markers"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        if not _pack():
            return False
        ob = context.active_object
        return ob is not None and ob.type == 'MESH'

    def execute(self, context):
        ob = context.active_object
        pos, H, _cx = guess_markers(ob)
        coll = _marker_collection(context.scene)
        for key in MARKERS:
            name = marker_name(key)
            e = bpy.data.objects.get(name)
            if e is None:
                e = bpy.data.objects.new(name, None)
                coll.objects.link(e)
            e.empty_display_type = 'SPHERE'
            e.empty_display_size = 0.018 * H
            e.show_in_front = True
            e.show_name = True
            e.location = pos[key]
        context.scene["enso_rig_target"] = ob.name
        self.report({'INFO'}, "Markers placed. Move any that are off, then Build Rig")
        return {'FINISHED'}


# ------------------------------------------------------------------------------------- rig

def _read_markers():
    out = {}
    for key in MARKERS:
        e = bpy.data.objects.get(marker_name(key))
        if e is None:
            return None
        out[key] = e.matrix_world.translation.copy()
    return out


def build_armature(context, mesh, m, controls=True):
    pts = _mesh_points(mesh)
    zmin = min(p.z for p in pts); zmax = max(p.z for p in pts)
    H = max(zmax - zmin, 1e-4)
    cx = m["groin"].x
    cy = m["groin"].y

    def mir(p):
        return Vector((2 * cx - p.x, p.y, p.z))

    sh, el, wr, kn, an = m["shoulder.L"], m["elbow.L"], m["wrist.L"], m["knee.L"], m["ankle.L"]
    pelvis = Vector((cx, cy, m["groin"].z + 0.035 * H))
    neck = Vector((cx, cy + 0.01 * H, sh.z + 0.02 * H))
    head = Vector((cx, cy, m["chin"].z + 0.012 * H))
    top = Vector((cx, cy, zmax))
    sp1 = pelvis.lerp(neck, 0.30)
    sp2 = pelvis.lerp(neck, 0.62)
    # The hand runs from the wrist to the fingertips, found on the mesh itself.
    along = (wr - el).normalized()
    reach = [(p - wr).dot(along) for p in pts
             if 0.0 < (p - wr).dot(along) < 0.17 * H and ((p - wr) - along * (p - wr).dot(along)).length < 0.07 * H]
    hand_len = min(max(max(reach, default=0.1 * H), 0.06 * H), 0.15 * H)
    hand_end = wr + along * hand_len * 0.45
    fing_mid = wr + along * hand_len * 0.74
    fing_end = wr + along * hand_len
    hip = Vector((cx + (kn.x - cx) * 0.95, cy, m["groin"].z + 0.03 * H))
    toe = Vector((an.x, an.y - 0.085 * H, zmin + 0.012 * H))
    toe_end = Vector((an.x, an.y - 0.13 * H, zmin + 0.012 * H))
    clav = Vector((cx + 0.02 * H, cy, sh.z + 0.005 * H))

    data = bpy.data.armatures.new(mesh.name + " Rig")
    arm = bpy.data.objects.new(mesh.name + " Rig", data)
    context.scene.collection.objects.link(arm)
    data.display_type = 'OCTAHEDRAL'
    arm.show_in_front = True
    for o in context.view_layer.objects:
        o.select_set(False)
    context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = data.edit_bones

    def bone(name, a, b, parent=None, connect=False):
        e = eb.new(name)
        e.head, e.tail = a, b
        if parent:
            e.parent = eb[parent]
            e.use_connect = connect
        return e

    bone("hips", pelvis, sp1)
    bone("spine", sp1, sp2, "hips", True)
    bone("chest", sp2, neck, "spine", True)
    bone("neck", neck, head, "chest", True)
    bone("head", head, top, "neck", True)
    for side, f in ((".L", lambda p: p), (".R", mir)):
        bone("shoulder" + side, f(clav), f(sh), "chest")
        bone("upper_arm" + side, f(sh), f(el), "shoulder" + side, True)
        bone("forearm" + side, f(el), f(wr), "upper_arm" + side, True)
        bone("hand" + side, f(wr), f(hand_end), "forearm" + side, True)
        bone("fingers1" + side, f(hand_end), f(fing_mid), "hand" + side, True)
        bone("fingers2" + side, f(fing_mid), f(fing_end), "fingers1" + side, True)
        bone("thigh" + side, f(hip), f(kn), "hips")
        bone("shin" + side, f(kn), f(an), "thigh" + side, True)
        bone("foot" + side, f(an), f(toe), "shin" + side, True)
        bone("toe" + side, f(toe), f(toe_end), "foot" + side, True)
    for e in eb:
        e.align_roll(Vector((0, -1, 0)) if abs(e.vector.normalized().y) < 0.9 else Vector((0, 0, 1)))
    bpy.ops.object.mode_set(mode='OBJECT')
    # Left red, right blue, middle gold: the usual animator colours.
    for b in data.bones:
        try:
            b.color.palette = 'THEME01' if b.name.endswith(".L") else ('THEME04' if b.name.endswith(".R") else 'THEME09')
        except Exception:
            pass
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    _bone_collections(data)
    if controls:
        _add_controls(context, arm, H)
    return arm


def _bone_collections(data):
    """Body, arms, legs and fingers can be shown and hidden on their own."""
    try:
        groups = {}
        for b in data.bones:
            n = b.name
            g = ("Fingers" if n.startswith("fingers") else
                 "Arms" if n.split(".")[0] in {"shoulder", "upper_arm", "forearm", "hand"} else
                 "Legs" if n.split(".")[0] in {"thigh", "shin", "foot", "toe"} else "Body")
            coll = groups.get(g) or data.collections.get(g) or data.collections.new(g)
            groups[g] = coll
            coll.assign(b)
    except Exception:
        pass


WIDGET_COLL = "Ensō Rig Widgets"


def _widget(name, verts, edges):
    ob = bpy.data.objects.get(name)
    if ob is not None:
        return ob
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, edges, [])
    ob = bpy.data.objects.new(name, me)
    coll = bpy.data.collections.get(WIDGET_COLL)
    if coll is None:
        coll = bpy.data.collections.new(WIDGET_COLL)
        coll.hide_viewport = True
        coll.hide_render = True
    coll.objects.link(ob)
    return ob


def _ring(n=32):
    v = [(math.cos(2 * math.pi * i / n), 0.0, math.sin(2 * math.pi * i / n)) for i in range(n)]
    return v, [(i, (i + 1) % n) for i in range(n)]


def _box():
    v = [(x, y, z) for x in (-1, 1) for y in (0, 1) for z in (-1, 1)]
    e = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]
    return v, e


def _add_controls(context, arm, H):
    """Animator controls: rings around the body and limbs, boxes on hands and feet, drawn
    over the character so every part can be grabbed without hunting for a bone."""
    ring = _widget("WGT Ensō Ring", *_ring())
    box = _widget("WGT Ensō Box", *_box())
    # part -> (widget, radius as a part of the character's height, place along the bone)
    look = {
        "hips": (ring, 0.135, 0.2), "spine": (ring, 0.105, 0.5), "chest": (ring, 0.125, 0.6),
        "neck": (ring, 0.05, 0.5), "head": (ring, 0.085, 0.55),
        "shoulder": (ring, 0.03, 0.6), "upper_arm": (ring, 0.045, 0.5), "forearm": (ring, 0.038, 0.5),
        "hand": (box, 0.03, 0.0), "fingers1": (ring, 0.024, 0.5), "fingers2": (ring, 0.02, 0.5),
        "thigh": (ring, 0.07, 0.45), "shin": (ring, 0.05, 0.5), "foot": (box, 0.036, 0.0),
        "toe": (ring, 0.03, 0.5),
    }
    for pb in arm.pose.bones:
        spec = look.get(pb.name.split(".")[0])
        if spec is None:
            continue
        shape, radius, along = spec
        length = max(pb.bone.length, 1e-5)
        try:
            pb.custom_shape = shape
            pb.use_custom_shape_bone_size = False
            r = radius * H
            if shape is box:
                pb.custom_shape_scale_xyz = (r, length, r * 0.55)
            else:
                pb.custom_shape_scale_xyz = (r, r, r)
                pb.custom_shape_translation = (0.0, length * along, 0.0)
            pb.custom_shape_wire_width = 2.0
        except Exception:
            pass
    arm.data.show_bone_custom_shapes = True
    arm.show_in_front = True


def bind_mesh(context, mesh, arm):
    for o in context.view_layer.objects:
        o.select_set(False)
    for mod in list(mesh.modifiers):
        if mod.type == 'ARMATURE' and mod.object is None:
            mesh.modifiers.remove(mod)
    mesh.select_set(True)
    arm.select_set(True)
    context.view_layer.objects.active = arm
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    weighted = sum(1 for v in mesh.data.vertices if v.groups)
    _fix_loose(mesh, arm)
    return weighted, len(mesh.data.vertices)


def _fix_loose(mesh, arm):
    """Points the automatic weights missed (eyes, lashes, small floating parts) follow the
    nearest bone, so nothing is left behind when the character moves."""
    loose = [v for v in mesh.data.vertices if not v.groups]
    if not loose:
        return 0
    to_arm = arm.matrix_world.inverted() @ mesh.matrix_world
    bones = [(b.name, b.head_local, b.tail_local) for b in arm.data.bones]

    def dist(p, a, b):
        ab = b - a
        t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
        return (p - (a + ab * t)).length

    for v in loose:
        p = to_arm @ v.co
        name = min(bones, key=lambda bn: dist(p, bn[1], bn[2]))[0]
        vg = mesh.vertex_groups.get(name) or mesh.vertex_groups.new(name=name)
        vg.add([v.index], 1.0, 'REPLACE')
    return len(loose)


class OBJECT_OT_enso_rig_build(Operator):
    """Build the rig from the markers and bind the character to it with automatic weights"""
    bl_idname = "object.enso_rig_build"
    bl_label = "Build Rig"
    bl_options = {'REGISTER', 'UNDO'}

    animations: BoolProperty(
        name="Add Animations", default=True,
        description="Also write the Ensō animation clips for the new rig")
    keep_markers: BoolProperty(name="Keep Markers", default=False)
    controls: BoolProperty(
        name="Animator Controls", default=True,
        description="Show rings and boxes to grab instead of plain bones. "
                    "Turn off Shapes in the armature's Viewport Display to see the bones again")

    @classmethod
    def poll(cls, context):
        if not _pack():
            return False
        return bpy.data.objects.get(marker_name("chin")) is not None

    def execute(self, context):
        m = _read_markers()
        if m is None:
            self.report({'ERROR'}, "Markers are missing. Press Place Markers first")
            return {'CANCELLED'}
        mesh = bpy.data.objects.get(context.scene.get("enso_rig_target", ""))
        if mesh is None or mesh.type != 'MESH':
            mesh = context.active_object
        if mesh is None or mesh.type != 'MESH':
            self.report({'ERROR'}, "Select the character mesh")
            return {'CANCELLED'}
        if context.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        arm = build_armature(context, mesh, m, self.controls)
        extra = [o for o in context.selected_objects if o.type == 'MESH' and o is not mesh]
        done, total = bind_mesh(context, mesh, arm)
        for o in extra:
            bind_mesh(context, o, arm)
        if not self.keep_markers:
            for key in MARKERS:
                e = bpy.data.objects.get(marker_name(key))
                if e is not None:
                    bpy.data.objects.remove(e)
        n = 0
        if self.animations:
            n = len(write_clips(arm))
        context.view_layer.objects.active = arm
        if done < total * 0.9:
            self.report({'WARNING'}, "Rig built, but automatic weights reached only %d of %d points. "
                                     "The mesh may have loose or overlapping parts" % (done, total))
        else:
            self.report({'INFO'}, "Rig built and bound, %d animations added" % n)
        return {'FINISHED'}


# ------------------------------------------------------------------------------ animations

def _q(axis, deg):
    return Quaternion(axis, D(deg))


class Pose:
    """One frame: rotations given in the character's own axes (X left, -Y front, Z up), bone
    after bone down each chain, so a bend always follows the bone above it."""

    def __init__(self, rig):
        self.rig = rig
        self.rot = {}          # part -> rotation relative to its parent, in rest axes
        self.acc = {}          # part -> accumulated rotation of the chain down to it
        self.hips_offset = Vector((0, 0, 0))
        self.leg = {}          # side -> (thigh, knee) for grounding
        self.fist = {}         # side -> 0..1, overrides the clip's hand
        self.plant = 0.0       # how far forward the standing foot is from under the hips

    def set(self, part, q, parent=None):
        base = self.acc.get(parent, Quaternion()) if parent else Quaternion()
        # Express the wanted turn in the axes the parent has before it moves.
        local = base.inverted() @ q @ base
        self.rot[part] = local
        self.acc[part] = base @ local
        return self

    def bend(self, part, q, parent):
        """Turn a bone in its parent's own axes (a knee folding in the plane of its thigh)."""
        base = self.acc.get(parent, Quaternion())
        self.rot[part] = q
        self.acc[part] = base @ q
        return self

    def aim(self, part, direction, parent=None, extra=None):
        """Point a bone along a direction (character axes), plus an optional twist/bend."""
        rest = self.rig.rest_dir[part]
        base = self.acc.get(parent, Quaternion()) if parent else Quaternion()
        cur = base @ rest
        q = cur.rotation_difference(Vector(direction).normalized())
        if extra is not None:
            q = extra @ q
        return self.set(part, q, parent)


class RigInfo:
    def __init__(self, arm):
        self.arm = arm
        self.map = bone_map(arm)
        mw = arm.matrix_world.to_3x3()
        self.rest_rot = {}
        self.rest_dir = {}
        for part, name in self.map.items():
            b = arm.data.bones[name]
            r = (mw @ b.matrix_local.to_3x3()).to_quaternion()
            self.rest_rot[part] = r
            self.rest_dir[part] = (mw @ (b.tail_local - b.head_local)).normalized()
        zs = [(arm.matrix_world @ b.head_local).z for b in arm.data.bones] + \
             [(arm.matrix_world @ b.tail_local).z for b in arm.data.bones]
        self.height = max(max(zs) - min(zs), 1e-4)

        def length(part):
            n = self.map.get(part)
            return arm.data.bones[n].length * arm.matrix_world.to_scale().z if n else 0.25 * self.height
        self.thigh = length("thigh.L")
        self.shin = length("shin.L")

    def has(self, *parts):
        return all(p in self.map for p in parts)


def _ease(x):
    """Smooth 0..1."""
    x = min(max(x, 0.0), 1.0)
    return x * x * (3.0 - 2.0 * x)


def _lerp(a, b, t):
    return a + (b - a) * t


def _keys(t, keys):
    """Value at time t from (time, value) keys, eased between them. Values are numbers or
    tuples. For loops, give the first key again at time 1."""
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t <= t1:
            u = _ease((t - t0) / max(t1 - t0, 1e-6))
            if isinstance(v0, (tuple, list)):
                return tuple(_lerp(x, y, u) for x, y in zip(v0, v1))
            return _lerp(v0, v1, u)
    return keys[-1][1]


def _legs(p, side, thigh, knee, foot=None, spread=0.0, turn=0.0):
    """thigh: + forward. knee: + bent. foot None keeps the sole level with the ground.
    spread: + outward. turn: + toes outward. Angles in degrees."""
    s = 1.0 if side == ".L" else -1.0
    p.set("thigh" + side, _q(Z, turn * s) @ _q(Y, -spread * s) @ _q(X, -thigh))
    p.bend("shin" + side, _q(X, knee), "thigh" + side)
    if foot is None:
        foot = -(knee - thigh)
    p.bend("foot" + side, _q(X, foot) @ _q(Y, spread * s), "shin" + side)
    p.bend("toe" + side, Quaternion(), "foot" + side)
    p.leg[side] = (thigh, knee, spread)


def _ground(p, rig, air=0.0, extra=0.0):
    """Drop the hips so the lower foot stands on the ground: this is what gives a walk its
    weight (down on contact, up over the standing leg). air lifts the body in flight phases."""
    best = None
    fwd = 0.0
    for side, (a, k, sp) in p.leg.items():
        ext = (rig.thigh * math.cos(D(a)) + rig.shin * math.cos(D(k - a))) * math.cos(D(sp))
        if best is None or ext > best:
            best = ext
            fwd = -rig.thigh * math.sin(D(a)) + rig.shin * math.sin(D(k - a))
    if best is None:
        return
    p.hips_offset = Vector((0, 0, best - (rig.thigh + rig.shin) + air * rig.height + extra * rig.height))
    p.plant = fwd


def _arm(p, side, direction, elbow, parent="chest", hand=0.0):
    """direction: where the upper arm points (character axes, for the left arm; X is mirrored
    for the right). elbow: bend in degrees; the forearm always folds toward the front and up,
    the way an arm really bends. hand: wrist bend."""
    s = 1.0 if side == ".L" else -1.0
    d = Vector((direction[0] * s, direction[1], direction[2])).normalized()
    p.aim("upper_arm" + side, d, parent)
    hint = Vector((-0.25 * s, -1.0, 0.45)).normalized()
    perp = hint - d * hint.dot(d)
    if perp.length < 1e-4:
        perp = Vector((0, 0, 1)) - d * d.z
    perp.normalize()
    e = D(elbow)
    fore = d * math.cos(e) + perp * math.sin(e)
    p.aim("forearm" + side, fore, "upper_arm" + side)
    p.set("hand" + side, Quaternion(d.cross(perp).normalized(), D(hand)) if hand else Quaternion(), "forearm" + side)


def _torso(p, lean=0.0, twist=0.0, side=0.0, head=0.0, hips_twist=0.0, hips_lean=0.0, hips_side=0.0,
           look=0.0):
    """lean: + forward. twist: + turns the chest to the character's right. side: + tilts left.
    head: + chin down. look: + head turns right."""
    p.set("hips", _q(Z, hips_twist) @ _q(X, hips_lean) @ _q(Y, hips_side))
    p.set("spine", _q(Z, twist * 0.4) @ _q(X, lean * 0.4) @ _q(Y, side * 0.5 - hips_side * 0.6), "hips")
    p.set("chest", _q(Z, twist * 0.6) @ _q(X, lean * 0.6) @ _q(Y, side * 0.5 - hips_side * 0.4), "spine")
    # The head stays level and looks where the character is going.
    p.set("neck", _q(X, head * 0.5) @ _q(Z, (-twist - hips_twist) * 0.45 + look * 0.5), "chest")
    p.set("head", _q(X, head * 0.5) @ _q(Z, (-twist - hips_twist) * 0.45 + look * 0.5), "neck")


def _sin(t, shift=0.0):
    return math.sin(2 * math.pi * (t + shift))


def _step(t, ph, stride, lift, stance=0.5):
    """One leg of a walk or run cycle. Returns (thigh, knee, foot roll).
    The leg is on the ground for `stance` of the cycle, sweeping back, then swings forward
    with the knee folded."""
    u = (t + ph) % 1.0
    if u < stance:                       # on the ground: heel strike -> toe off
        v = u / stance
        thigh = _lerp(stride, -stride * 0.9, v)
        knee = 6 + 14 * math.sin(v * math.pi) ** 2 * 0.6 + (10 if v < 0.15 else 0) * (1 - v / 0.15)
        roll = -14 * max(0.0, 1 - v * 5) + 26 * max(0.0, (v - 0.75) / 0.25)
        return thigh, knee, roll
    v = (u - stance) / (1.0 - stance)    # in the air: fold, swing through, reach
    thigh = _lerp(-stride * 0.9, stride, _ease(v) ** 0.85)
    knee = 6 + lift * math.sin(min(v * 1.15, 1.0) * math.pi) ** 1.3
    roll = 22 * (1 - v) - 14 * max(0.0, (v - 0.8) / 0.2)
    return thigh, knee, roll


def _walk_like(rig, t, stride, lift, stance, lean, twist, arm, elbow, air=0.0, arm_back=0.0, head=0.0,
               sway=3.0):
    p = Pose(rig)
    sw = _sin(t, 0.06)
    for s, ph in ((".L", 0.0), (".R", 0.5)):
        th, kn, roll = _step(t, ph, stride, lift, stance)
        _legs(p, s, th, kn, foot=-(kn - th) + roll)
    # Hips tilt toward the leg in the air, the chest tilts back: the classic counter pose.
    tilt = sway * _sin(t, 0.25)
    _torso(p, lean=lean, twist=-twist * sw, hips_twist=twist * 0.6 * sw, hips_side=tilt, side=-tilt * 0.4,
           head=head)
    for s, ph in ((".L", 0.5), (".R", 0.0)):
        a = _sin(t, ph + 0.04)           # arms a touch late: overlap
        late = _sin(t, ph - 0.03)
        _arm(p, s, (0.13, -arm * a + arm_back, -1.0), elbow[0] + elbow[1] * max(0.0, a) + 6 * late,
             hand=-10 * late)
    _ground(p, rig, air=air * max(0.0, math.sin(4 * math.pi * (t + 0.02))) if air else 0.0)
    return p


def clip_idle(rig, t):
    p = Pose(rig)
    b = _sin(t)
    late = _sin(t, -0.08)
    # Weight on the left leg, right knee soft: a relaxed stand, not a soldier.
    _legs(p, ".L", 1.0, 2.0, spread=3.0)
    _legs(p, ".R", 4.0, 9.0 + 2 * b, spread=6.0, turn=10.0)
    _torso(p, lean=1.5 + 1.5 * b, hips_side=-4.0, side=3.0 + 0.6 * late, head=-2.0 - 1.0 * late, look=6.0)
    _arm(p, ".L", (0.17, 0.05, -1.0), 10 + 2 * late, hand=-6)
    _arm(p, ".R", (0.20, -0.04, -1.0), 16 + 3 * late, hand=-10)
    _ground(p, rig, extra=-0.002 * (1 + b))
    return p


def clip_walk(rig, t):
    return _walk_like(rig, t, stride=26, lift=58, stance=0.6, lean=3.0, twist=9.0, arm=0.42,
                      elbow=(10, 22), sway=4.0)


def clip_run(rig, t):
    return _walk_like(rig, t, stride=42, lift=105, stance=0.36, lean=16.0, twist=16.0, arm=0.85,
                      elbow=(82, 20), air=0.035, head=-10.0, sway=2.0)


def clip_anime_run(rig, t):
    """The anime sprint: deep lean, huge strides, fists pumping close to the body."""
    return _walk_like(rig, t, stride=56, lift=125, stance=0.28, lean=30.0, twist=20.0, arm=1.0,
                      elbow=(105, 12), air=0.06, head=-24.0, sway=1.0)


def clip_ninja_run(rig, t):
    p = Pose(rig)
    sw = _sin(t, 0.06)
    for s, ph in ((".L", 0.0), (".R", 0.5)):
        th, kn, roll = _step(t, ph, 54, 125, 0.28)
        _legs(p, s, th + 6, kn, foot=-(kn - th) + roll)
    _torso(p, lean=58.0, twist=-5 * sw, hips_twist=8 * sw, hips_lean=10.0, head=-44.0)
    for s, ph in ((".L", 0.0), (".R", 0.5)):
        flap = _sin(t * 2, ph)
        # Arms straight back, hands open, trailing behind like a cape.
        _arm(p, s, (0.24, 1.0, 0.16 + 0.04 * flap), 3, hand=14)
    _ground(p, rig, air=0.05 * max(0.0, math.sin(4 * math.pi * (t + 0.02))))
    return p


def clip_goofy_run(rig, t):
    """Rubber-limbed cartoon run: knees high, arms thrown up and flailing, big grin energy."""
    p = Pose(rig)
    sw = _sin(t)
    for s, ph in ((".L", 0.0), (".R", 0.5)):
        th, kn, roll = _step(t, ph, 62, 135, 0.3)
        _legs(p, s, th + 14, kn, foot=-(kn - th) + roll, spread=7)
    _torso(p, lean=-6.0, twist=-22 * sw, hips_twist=14 * sw, head=-14.0, side=6 * _sin(t, 0.25))
    for s, ph in ((".L", 0.0), (".R", 0.5)):
        a = _sin(t, ph)
        c = _sin(t * 2, ph)
        _arm(p, s, (0.55 + 0.25 * c, -0.25 + 0.5 * a, 1.0), 25 + 30 * max(0.0, c), hand=30 * a)
    _ground(p, rig, air=0.07 * max(0.0, math.sin(4 * math.pi * (t + 0.02))))
    return p


def clip_fight_idle(rig, t):
    """Guard stance, light on the feet."""
    p = Pose(rig)
    b = _sin(t * 2)
    k = 26 + 5 * b
    _legs(p, ".L", 26, k + 14, spread=10, turn=-4)
    _legs(p, ".R", -18, k + 6, foot=-(k + 6 + 18) + 22, spread=12, turn=24)
    _torso(p, lean=10, twist=14, hips_twist=12, head=-2, look=-26)
    _arm(p, ".L", (0.26, -0.5, -0.8 + 0.03 * b), 100, hand=10)
    _arm(p, ".R", (0.32, -0.1, -0.95 + 0.03 * b), 122, hand=10)
    _ground(p, rig)
    return p


def clip_crouch(rig, t):
    p = Pose(rig)
    b = _sin(t)
    a, k = 78 + 1.5 * b, 118 + 2.5 * b
    _torso(p, lean=24 + b, head=-14.0)
    for s in (".L", ".R"):
        _legs(p, s, a, k, spread=10, turn=8)
    _arm(p, ".L", (0.20, -0.75, -0.55), 70, hand=20)
    _arm(p, ".R", (0.20, -0.75, -0.55), 70, hand=20)
    _ground(p, rig)
    p.hips_offset.y = p.plant
    return p


def clip_jump(rig, t):
    """Not a loop: sink, explode up, tuck, land and absorb."""
    p = Pose(rig)
    # time, (thigh, knee, height, arm swing, lean)
    a, k, up, arms, lean = _keys(t, (
        (0.00, (2, 4, 0.0, 0.0, 2)),
        (0.22, (70, 112, 0.0, 0.9, 30)),     # deep anticipation, arms back
        (0.34, (4, 6, 0.10, -1.3, -6)),      # full stretch at take off, arms thrown up
        (0.50, (58, 100, 0.34, -1.1, 6)),    # tuck at the top
        (0.66, (14, 24, 0.14, -0.2, 0)),     # legs reach for the ground
        (0.76, (62, 104, 0.0, 0.7, 26)),     # land, absorb
        (1.00, (2, 4, 0.0, 0.0, 2)),
    ))
    _torso(p, lean=lean, head=-lean * 0.5)
    for s in (".L", ".R"):
        _legs(p, s, a, k, foot=None if up < 0.02 else 34, spread=5)
        _arm(p, s, (0.2 + 0.1 * max(0.0, -arms), arms, -1.0 if arms > -0.6 else 0.9), 18 + 30 * max(0.0, arms))
    _ground(p, rig, air=up)
    if up < 0.02:
        p.hips_offset.y = p.plant
    return p


def clip_punch(rig, t):
    """Not a loop: guard, wind up, right straight with the whole body behind it, recoil."""
    p = Pose(rig)
    hit, wind = _keys(t, ((0.0, (0.0, 0.0)), (0.2, (0.0, 1.0)), (0.34, (1.0, 0.0)), (0.5, (1.0, 0.0)),
                          (0.8, (0.0, 0.0)), (1.0, (0.0, 0.0))))
    _legs(p, ".L", 30 + 10 * hit, 44 + 8 * hit, spread=10, turn=-4)
    _legs(p, ".R", -22 + 6 * hit, 30 - 14 * hit, foot=-(30 + 22) + 26 + 14 * hit, spread=12, turn=26)
    _torso(p, lean=10 + 8 * hit, twist=14 + 16 * wind - 52 * hit, hips_twist=12 + 6 * wind - 28 * hit,
           head=-2.0, look=-26 + 34 * hit - 8 * wind)
    _arm(p, ".L", tuple(Vector((0.26, -0.5, -0.8)).lerp(Vector((0.42, -0.05, -0.8)), hit)), 100 + 25 * hit)
    guard = Vector((0.32, -0.1, -0.95)).lerp(Vector((0.45, 0.3, -0.75)), wind)
    out = Vector((0.02, -1.0, 0.10))
    _arm(p, ".R", tuple(guard.lerp(out, hit)), (122 + 10 * wind) * (1 - hit) + 2, hand=-8 * hit)
    _ground(p, rig)
    p.hips_offset.y = -0.03 * rig.height * hit + 0.01 * rig.height * wind
    return p


def clip_kick(rig, t):
    """Not a loop: chamber the knee, snap a high front kick, pull back."""
    p = Pose(rig)
    lift, snap = _keys(t, ((0.0, (0.0, 0.0)), (0.25, (1.0, 0.0)), (0.38, (1.0, 1.0)), (0.48, (1.0, 1.0)),
                           (0.62, (1.0, 0.0)), (1.0, (0.0, 0.0))))
    _torso(p, lean=8 - 26 * snap, twist=20 - 10 * lift, hips_twist=14, hips_lean=-8 * snap, head=6 * snap,
           look=-30)
    _legs(p, ".L", 4 - 10 * snap, 20 - 8 * snap, spread=4, turn=-24)
    _legs(p, ".R", (-16) * (1 - lift) + lift * (96 + 14 * snap), 26 * (1 - lift) + lift * (128 * (1 - snap) + 3),
          foot=-30 * snap + 20 * lift * (1 - snap), spread=4)
    _arm(p, ".L", (0.26, -0.5, -0.8), 100)
    _arm(p, ".R", (0.40, 0.2 * snap - 0.3, -0.8), 100 - 30 * snap)
    p.leg.pop(".R")
    _ground(p, rig)
    return p


def clip_power_up(rig, t):
    """Charging up: wide stance, fists clenched low, the whole body shaking with effort."""
    p = Pose(rig)
    shake = math.sin(2 * math.pi * t * 12) * 0.8
    rise = 0.5 - 0.5 * math.cos(2 * math.pi * t)
    for s in (".L", ".R"):
        _legs(p, s, 34 - 4 * rise, 62 - 8 * rise, spread=30, turn=30)
        _arm(p, s, (0.42 + 0.01 * shake, -0.22, -0.9), 96 + 6 * rise + shake, hand=14)
    _torso(p, lean=8 - 16 * rise + shake * 0.5, head=10 - 30 * rise + shake)
    _ground(p, rig, extra=0.0015 * shake)
    p.hips_offset.y = p.plant
    return p


def clip_hero_landing(rig, t):
    """Not a loop: drop in, one knee and one fist on the ground, look up, stand."""
    p = Pose(rig)
    d, look = _keys(t, ((0.0, (0.0, 0.0)), (0.12, (1.15, 0.0)), (0.2, (1.0, 0.0)), (0.5, (1.0, 1.0)),
                        (0.72, (1.0, 1.0)), (1.0, (0.0, 0.0))))
    air = _keys(t, ((0.0, 0.5), (0.1, 0.0), (1.0, 0.0)))
    _legs(p, ".L", 96 * d, 118 * d, spread=10 * d, turn=8)
    _legs(p, ".R", 6 * d, 122 * d, foot=-116 * d + 50 * d, spread=6 * d)
    _torso(p, lean=44 * d, twist=-16 * d, head=18 * d - 46 * look * d, look=0)
    _arm(p, ".L", (0.30 + 0.5 * d, 0.55 * d, -1.0 + 0.7 * d), 12 + 20 * d)
    _arm(p, ".R", (0.16, -0.5 * d, -1.0), 10 + 8 * d, hand=-40 * d)
    p.leg.pop(".R")
    _ground(p, rig, air=air)
    p.hips_offset.y = p.plant * 0.6
    return p


def clip_victory(rig, t):
    """Fist to the sky, a little hop of joy."""
    p = Pose(rig)
    hop = max(0.0, _sin(t * 2)) ** 2
    b = _sin(t * 2, -0.1)
    _legs(p, ".L", 6 - 4 * hop, 12 - 8 * hop, spread=7, turn=8)
    _legs(p, ".R", 6 - 4 * hop, 12 - 8 * hop, spread=7, turn=8)
    _torso(p, lean=-8, side=-5, twist=-10, head=-16, look=8)
    _arm(p, ".R", (0.16, -0.12, 1.0), 6 + 10 * b, hand=0)
    _arm(p, ".L", (0.34, -0.35, -0.8), 112, hand=12)
    _ground(p, rig, air=0.03 * hop)
    return p


def clip_wave(rig, t):
    """Friendly wave."""
    p = Pose(rig)
    w = _sin(t * 3)
    b = _sin(t)
    _legs(p, ".L", 1.0, 2.0, spread=3.0)
    _legs(p, ".R", 4.0, 9.0, spread=6.0, turn=10.0)
    _torso(p, lean=1.0 + b, hips_side=-4.0, side=5.0, head=-4.0, look=4.0 + 4 * w)
    _arm(p, ".L", (0.17, 0.05, -1.0), 10, hand=-6)
    _arm(p, ".R", (0.75 + 0.12 * w, -0.25, 0.35), 78 + 16 * w, hand=18 * w)
    _ground(p, rig)
    return p


def clip_swim(rig, t):
    """Front crawl, body level."""
    p = Pose(rig)
    roll = 14 * _sin(t)
    p.set("hips", _q(Y, roll) @ _q(X, 84))
    p.set("spine", _q(X, -3), "hips")
    p.set("chest", _q(X, -5), "spine")
    p.set("neck", _q(X, -24), "chest")
    p.set("head", _q(X, -28) @ _q(Z, -roll * 1.5), "neck")
    for s, ph in ((".L", 0.0), (".R", 0.5)):
        kick = 16 * _sin(t * 3, ph)
        p.set("thigh" + s, _q(X, -kick), "hips")
        p.set("shin" + s, _q(X, 10 + 10 * max(0.0, _sin(t * 3, ph + 0.2))), "thigh" + s)
        p.set("foot" + s, _q(X, 48), "shin" + s)
        # The arm turns one smooth circle around the shoulder: overhead, down through the
        # water, back along the body, then out of the water with a bent elbow.
        k = 1.0 if s == ".L" else -1.0
        base = p.acc["chest"]
        axis = base @ X
        overhead = base @ Vector((0.14 * k, 0.0, 1.0)).normalized()
        q_ref = (base @ rig.rest_dir["upper_arm" + s]).rotation_difference(overhead)
        ang = 2 * math.pi * ((t + ph) % 1.0)
        p.set("upper_arm" + s, Quaternion(axis, ang) @ q_ref, "chest")
        recover = max(0.0, -math.sin(ang))
        p.set("forearm" + s, Quaternion(axis, D(75 * recover)), "upper_arm" + s)
    p.hips_offset = Vector((0, 0, 0.3 * rig.height))
    return p


# Raise when a clip changes, so files made earlier get the new motion.
CLIP_VERSION = 4

# How closed the hands are in each clip (0 open, 1 fist). Others stay relaxed.
FISTS = {"Run": 1.0, "Anime Run": 1.0, "Goofy Run": 0.0, "Fight Idle": 1.0, "Punch": 1.0, "Kick": 1.0,
         "Power Up": 1.0, "Hero Landing": 1.0, "Victory": 1.0, "Wave": 0.0, "Swim": 0.0, "Ninja Run": 0.0,
         "Crouch": 0.1}

# name: (function, frames, loops, description)
CLIPS = {
    "Idle": (clip_idle, 80, True, "Relaxed stand, weight on one leg, breathing"),
    "Fight Idle": (clip_fight_idle, 40, True, "Guard stance, bouncing lightly"),
    "Walk": (clip_walk, 32, True, "Walk with weight: heel strike, hip sway, loose arms"),
    "Run": (clip_run, 22, True, "Run with a flight phase"),
    "Anime Run": (clip_anime_run, 16, True, "Anime sprint: deep lean, huge strides, pumping fists"),
    "Ninja Run": (clip_ninja_run, 16, True, "Bent far forward with both arms trailing straight back"),
    "Goofy Run": (clip_goofy_run, 16, True, "Cartoon run with high knees and flailing arms"),
    "Crouch": (clip_crouch, 60, True, "Low crouch, breathing"),
    "Jump": (clip_jump, 36, False, "Sink, jump, tuck and land"),
    "Punch": (clip_punch, 26, False, "Wind up and a right straight with the whole body"),
    "Kick": (clip_kick, 30, False, "Chambered high front kick"),
    "Power Up": (clip_power_up, 48, True, "Charging stance, body shaking with effort"),
    "Hero Landing": (clip_hero_landing, 44, False, "Drop to one knee and a fist, look up, rise"),
    "Victory": (clip_victory, 40, True, "Fist in the air with a happy hop"),
    "Wave": (clip_wave, 48, True, "Friendly wave"),
    "Swim": (clip_swim, 40, True, "Front crawl"),
}


def action_name(arm, clip):
    return "Ensō %s · %s" % (clip, arm.name)


def _fcurves(action):
    seen = set()
    try:
        for layer in action.layers:
            for strip in layer.strips:
                for bag in getattr(strip, "channelbags", ()):
                    for fc in bag.fcurves:
                        if fc.as_pointer() not in seen:
                            seen.add(fc.as_pointer())
                            yield fc
    except AttributeError:
        pass
    try:
        for fc in action.fcurves:
            if fc.as_pointer() not in seen:
                seen.add(fc.as_pointer())
                yield fc
    except (AttributeError, RuntimeError):
        pass


def _assign(arm, action):
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = action
    try:
        if getattr(ad, "action_slot", None) is None and len(action.slots):
            ad.action_slot = action.slots[0]
    except Exception:
        pass


def write_clip(arm, clip):
    """Write one clip as an action for this armature. Returns the action, or None."""
    fn, frames, loops, desc = CLIPS[clip]
    rig = RigInfo(arm)
    if not rig.has("hips", "thigh.L", "thigh.R", "upper_arm.L", "upper_arm.R"):
        return None
    name = action_name(arm, clip)
    old = bpy.data.actions.get(name)
    if old is not None:
        bpy.data.actions.remove(old)
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    prev_action = arm.animation_data.action if arm.animation_data else None
    _assign(arm, action)
    last = {}
    inv_world = arm.matrix_world.to_3x3().inverted()
    hips_pb = arm.pose.bones[rig.map["hips"]]
    hips_rest = arm.data.bones[rig.map["hips"]].matrix_local.to_3x3()
    count = frames + 1 if loops else frames
    fist = FISTS.get(clip, 0.22)
    for i in range(count):
        t = (i / frames) if loops else (i / max(frames - 1, 1))
        pose = fn(rig, t % 1.0 if loops else t)
        frame = 1 + i
        for part, name_b in rig.map.items():
            pb = arm.pose.bones[name_b]
            pb.rotation_mode = 'QUATERNION'
            q = pose.rot.get(part, Quaternion())
            r = rig.rest_rot[part]
            local = r.inverted() @ q @ r
            if part in last and last[part].dot(local) < 0.0:
                local = -local
            last[part] = local.copy()
            pb.rotation_quaternion = local
            pb.keyframe_insert("rotation_quaternion", frame=frame)
        for side in (".L", ".R"):
            f = pose.fist.get(side, fist)
            for nb, deg in (("fingers1" + side, 78.0), ("fingers2" + side, 96.0)):
                pb = arm.pose.bones.get(nb)
                if pb is None:
                    continue
                # Curl toward the palm, which faces down in the rest pose.
                r3 = pb.bone.matrix_local.to_3x3()
                d = (r3 @ Vector((0, 1, 0))).normalized()
                n = Vector((0, 0, -1)) - d * (-d.z)
                if n.length < 1e-4:
                    n = Vector((0, -1, 0))
                axis = r3.inverted() @ d.cross(n.normalized())
                pb.rotation_mode = 'QUATERNION'
                pb.rotation_quaternion = Quaternion(axis, D(deg * f))
                pb.keyframe_insert("rotation_quaternion", frame=frame)
        hips_pb.location = hips_rest.inverted() @ (inv_world @ pose.hips_offset)
        hips_pb.keyframe_insert("location", frame=frame)
    for fc in _fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = 'BEZIER'
            kp.handle_left_type = kp.handle_right_type = 'AUTO_CLAMPED'
        fc.update()
        if loops and not any(m.type == 'CYCLES' for m in fc.modifiers):
            fc.modifiers.new('CYCLES')
    action["enso_clip"] = clip
    action["enso_version"] = CLIP_VERSION
    action["enso_frames"] = frames
    action["enso_loops"] = loops
    try:
        action.asset_mark()
        action.asset_data.description = desc + " (Ensō Quick Rig, for %s)" % arm.name
        action.asset_data.author = "Mix 3D Design"
        action.asset_data.tags.new("Ensō")
        action.asset_data.tags.new("animation")
    except Exception:
        pass
    if prev_action is not None:
        _assign(arm, prev_action)
    return action


def write_clips(arm):
    out = []
    for clip in CLIPS:
        a = write_clip(arm, clip)
        if a is not None:
            out.append(a)
    reset_pose(arm)
    return out


def reset_pose(arm):
    for pb in arm.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)


def apply_clip(arm, clip, set_range=True, scene=None):
    action = bpy.data.actions.get(action_name(arm, clip))
    if action is None or action.get("enso_version", 0) != CLIP_VERSION:
        # Missing, or written by an older Ensō: write it again so fixes reach old files.
        action = write_clip(arm, clip)
    if action is None:
        return None
    _assign(arm, action)
    if set_range and scene is not None:
        frames = int(action.get("enso_frames", 24))
        scene.frame_start = 1
        scene.frame_end = frames if action.get("enso_loops", True) else frames
    return action


class ARMATURE_OT_enso_rig_clip(Operator):
    """Play this Ensō animation on the active armature. It is written for the rig the first """ \
        """time, and works on rigs with Mixamo, Rigify, VRM or VRoid bone names too"""
    bl_idname = "armature.enso_rig_clip"
    bl_label = "Ensō Animation"
    bl_options = {'REGISTER', 'UNDO'}

    clip: EnumProperty(name="Animation", items=[(k, k, v[3]) for k, v in CLIPS.items()])
    set_range: BoolProperty(name="Set Frame Range", default=True,
                            description="Set the scene's start and end frame to the clip")

    @classmethod
    def poll(cls, context):
        if not _pack():
            return False
        ob = context.active_object
        return ob is not None and ob.type == 'ARMATURE'

    def execute(self, context):
        arm = context.active_object
        if context.mode not in {'OBJECT', 'POSE'}:
            bpy.ops.object.mode_set(mode='OBJECT')
        action = apply_clip(arm, self.clip, self.set_range, context.scene)
        if action is None:
            self.report({'ERROR'}, "This armature's bones are not recognised. Ensō reads Ensō, Mixamo, "
                                   "Rigify, VRM and VRoid bone names")
            return {'CANCELLED'}
        self.report({'INFO'}, "%s on %s" % (self.clip, arm.name))
        return {'FINISHED'}


class ARMATURE_OT_enso_rig_rest(Operator):
    """Back to the rest pose: removes the animation from the armature (the clips are kept)"""
    bl_idname = "armature.enso_rig_rest"
    bl_label = "Rest Pose"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        if not _pack():
            return False
        ob = context.active_object
        return ob is not None and ob.type == 'ARMATURE'

    def execute(self, context):
        arm = context.active_object
        if arm.animation_data:
            arm.animation_data.action = None
        reset_pose(arm)
        return {'FINISHED'}


class VIEW3D_PT_enso_quick_rig(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "Ensō"
    bl_label = "Quick Rig"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return _pack()

    def draw(self, context):
        layout = self.layout
        ob = context.active_object
        col = layout.column(align=True)
        col.label(text="1. Select the character, then:")
        col.operator("object.enso_rig_markers", icon='EMPTY_AXIS')
        col.label(text="2. Move markers that are off, then:")
        col.operator("object.enso_rig_build", icon='ARMATURE_DATA')
        layout.separator()
        col = layout.column(align=True)
        col.label(text="3. Animations (select the rig)")
        col.enabled = ob is not None and ob.type == 'ARMATURE'
        grid = col.grid_flow(row_major=True, columns=2, even_columns=True, align=True)
        current = ""
        if ob is not None and ob.type == 'ARMATURE' and ob.animation_data and ob.animation_data.action:
            current = ob.animation_data.action.get("enso_clip", "")
        for clip in CLIPS:
            grid.operator("armature.enso_rig_clip", text=clip, depress=(clip == current)).clip = clip
        col.operator("armature.enso_rig_rest", icon='LOOP_BACK')
        layout.label(text="Works on Mixamo, Rigify, VRM rigs too", icon='INFO')


class VIEW3D_MT_enso_quick_rig(bpy.types.Menu):
    bl_label = "Ensō Quick Rig"
    bl_idname = "VIEW3D_MT_enso_quick_rig"

    def draw(self, context):
        layout = self.layout
        ob = context.active_object
        layout.operator("object.enso_rig_markers", text="1. Place Markers", icon='EMPTY_AXIS')
        layout.operator("object.enso_rig_build", text="2. Build Rig", icon='ARMATURE_DATA')
        layout.separator()
        col = layout.column()
        col.enabled = ob is not None and ob.type == 'ARMATURE'
        col.label(text="3. Play On The Selected Rig" if col.enabled else "3. Select a rig to play:")
        for clip in CLIPS:
            col.operator("armature.enso_rig_clip", text=clip, icon='PLAY').clip = clip
        col.separator()
        col.operator("armature.enso_rig_rest", icon='LOOP_BACK')


def _menu(self, context):
    if not _pack():
        return
    self.layout.separator()
    self.layout.menu("VIEW3D_MT_enso_quick_rig", icon='ARMATURE_DATA')


_MENUS = ("VIEW3D_MT_object", "ENSO_MT_topbar")
_appended = set()


def _append_menus():
    waiting = False
    for name in _MENUS:
        if name in _appended:
            continue
        menu = getattr(bpy.types, name, None)
        if menu is None:
            # The Ensō top menu belongs to the pack and may be turned on later.
            waiting = True
            continue
        menu.append(_menu)
        _appended.add(name)
    return 3.0 if waiting else None


classes = (
    VIEW3D_MT_enso_quick_rig,
    OBJECT_OT_enso_rig_markers,
    OBJECT_OT_enso_rig_build,
    ARMATURE_OT_enso_rig_clip,
    ARMATURE_OT_enso_rig_rest,
    VIEW3D_PT_enso_quick_rig,
)


def register():
    # The classes are registered by bl_operators itself.
    bpy.app.timers.register(_append_menus, first_interval=0.5, persistent=True)


def unregister():
    for name in list(_appended):
        menu = getattr(bpy.types, name, None)
        if menu is not None:
            menu.remove(_menu)
    _appended.clear()
