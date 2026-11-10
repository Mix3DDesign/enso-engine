# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō Face Map: bakes an anime face shadow map (the "SDF" face map) straight from the mesh.

No painting and no Cycles. For every pixel of the face the light is swept around the head and
the map stores how far it can turn before the pixel goes dark. Red is the light on the
character's right, green on the left, so the face does not have to be symmetric and the UV
map does not have to be mirrored. The map is read by the Ensō Face Map shader node.
"""

import math

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty, StringProperty
from bpy.types import Operator


AUTO_UV = "Enso Face Map"


def _np():
    import numpy
    return numpy


def _blur(a, sigma):
    np = _np()
    if sigma <= 0.05:
        return a
    r = max(1, int(sigma * 3.0 + 0.5))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    p = np.pad(a, ((r, r), (0, 0)), mode='edge')
    a = sum(p[i:i + a.shape[0], :] * k[i] for i in range(2 * r + 1))
    p = np.pad(a, ((0, 0), (r, r)), mode='edge')
    return sum(p[:, i:i + a.shape[1]] * k[i] for i in range(2 * r + 1))


def _dilate(val, mask, passes):
    """Spread the values into the empty pixels so texture filtering never reads garbage."""
    np = _np()
    val = val.copy()
    mask = mask.copy()
    for _ in range(passes):
        if mask.all():
            break
        acc = np.zeros_like(val)
        cnt = np.zeros(mask.shape, dtype=np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            sv = np.roll(val, (dy, dx), axis=(0, 1))
            sm = np.roll(mask, (dy, dx), axis=(0, 1))
            acc += sv * sm
            cnt += sm
        new = (~mask) & (cnt > 0)
        val[new] = acc[new] / cnt[new]
        mask |= new
    return val, mask


def _resize(a, size):
    """Bilinear resize of a square array."""
    np = _np()
    n = a.shape[0]
    if n == size:
        return a
    x = (np.arange(size) + 0.5) * n / size - 0.5
    x0 = np.clip(np.floor(x).astype(int), 0, n - 1)
    x1 = np.clip(x0 + 1, 0, n - 1)
    f = np.clip(x - x0, 0.0, 1.0).astype(np.float32)
    rows = a[x0, :] * (1 - f)[:, None] + a[x1, :] * f[:, None]
    return rows[:, x0] * (1 - f)[None, :] + rows[:, x1] * f[None, :]


def _write_png16(path, rgb):
    """16 bit RGB PNG. `rgb` is (height, width, 3) in 0..1 with the first row at the bottom."""
    import struct
    import zlib
    np = _np()
    h, w, _c = rgb.shape
    data = (np.clip(rgb[::-1], 0.0, 1.0) * 65535.0 + 0.5).astype('>u2')
    raw = np.zeros((h, 1 + w * 6), dtype=np.uint8)
    raw[:, 1:] = data.reshape(h, w * 3).view(np.uint8).reshape(h, w * 6)

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw.tobytes(), 6)))
        f.write(chunk(b"IEND", b""))


def _store_image(name, rgb):
    """Keep the map as a 16 bit image packed in the .blend file."""
    import os
    import tempfile
    path = os.path.join(tempfile.gettempdir(), bpy.path.clean_name(name) + ".png")
    _write_png16(path, rgb)
    image = bpy.data.images.get(name)
    if image is not None and image.packed_file is not None:
        image.unpack(method='REMOVE')
    if image is None:
        image = bpy.data.images.load(path, check_existing=False)
        image.name = name
    else:
        image.filepath = path
        image.source = 'FILE'
        image.reload()
    image.colorspace_settings.name = 'Non-Color'
    image.alpha_mode = 'NONE'
    try:
        image.pack()
    except Exception:
        pass
    return image


def head_frame(obj, poly_idx):
    """Center, forward and up of the head in the object's own space."""
    from mathutils import Vector
    me = obj.data
    inv = obj.matrix_world.inverted().to_3x3()
    up = (inv @ Vector((0.0, 0.0, 1.0))).normalized()
    fwd = Vector((0.0, 0.0, 0.0))
    lo = Vector((1e30, 1e30, 1e30))
    hi = Vector((-1e30, -1e30, -1e30))
    for i in poly_idx:
        p = me.polygons[i]
        fwd += p.normal * p.area
        for v in p.vertices:
            co = me.vertices[v].co
            lo = Vector((min(lo.x, co.x), min(lo.y, co.y), min(lo.z, co.z)))
            hi = Vector((max(hi.x, co.x), max(hi.y, co.y), max(hi.z, co.z)))
    fwd = fwd - up * fwd.dot(up)
    if fwd.length < 1e-8:
        fwd = inv @ Vector((0.0, -1.0, 0.0))
        fwd = fwd - up * fwd.dot(up)
    fwd.normalize()
    size = max((hi - lo).length, 1e-6)
    # The face is the front of the head: the middle of the head sits behind it.
    depth = abs((hi - lo).dot(fwd))
    width = abs((hi - lo).dot(up.cross(fwd)))
    center = (lo + hi) * 0.5 - fwd * max(width * 0.5 - depth * 0.5, 0.0)
    return center, fwd, up, size


def face_polygons(obj, group_name):
    me = obj.data
    vg = obj.vertex_groups.get(group_name) if group_name else None
    if vg is None:
        return [p.index for p in me.polygons]
    gi = vg.index
    inside = set()
    for v in me.vertices:
        for g in v.groups:
            if g.group == gi and g.weight > 0.5:
                inside.add(v.index)
                break
    polys = [p.index for p in me.polygons if all(v in inside for v in p.vertices)]
    return polys or [p.index for p in me.polygons]


def bake_face_map(obj, uv_name="", group="Face", size=1024, work=256, steps=48,
                  roundness=0.8, elevation=0.35, nose_shadow=True, smooth=2.5,
                  image_name="", occluders=()):
    """Bake the map. Returns (image, center, forward, up, uv map name)."""
    np = _np()
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree

    me = obj.data
    polys = face_polygons(obj, group)
    center, fwd, up, hsize = head_frame(obj, polys)
    right = up.cross(fwd).normalized()
    if uv_name:
        uvl = me.uv_layers.get(uv_name)
        if uvl is None:
            raise RuntimeError("UV map \"%s\" not found" % uv_name)
    else:
        # Own UV map: the face seen from the front. Everything that is not the front of the
        # face is parked in the corner, where the map says "not valid".
        polys = [i for i in polys if me.polygons[i].normal.dot(fwd) > -0.2]
        act = me.uv_layers.active_index
        uvl = me.uv_layers.get(AUTO_UV) or me.uv_layers.new(name=AUTO_UV, do_init=False)
        if uvl is None:
            raise RuntimeError("The mesh has no room for another UV map")
        if act >= 0:
            me.uv_layers.active_index = act
        uvl = me.uv_layers.get(AUTO_UV)
        vco = np.empty(len(me.vertices) * 3, dtype=np.float32)
        me.vertices.foreach_get("co", vco)
        vco = vco.reshape(-1, 3) - np.array(center, dtype=np.float32)
        px_ = vco @ np.array(right, dtype=np.float32)
        py_ = vco @ np.array(up, dtype=np.float32)
        used = sorted({v for i in polys for v in me.polygons[i].vertices})
        half = max(float(np.abs(px_[used]).max()), float(np.abs(py_[used]).max()), 1e-6)
        auto = np.zeros((len(me.loops), 2), dtype=np.float32)
        for i in polys:
            pl = me.polygons[i]
            for li, vi in zip(pl.loop_indices, pl.vertices):
                auto[li, 0] = 0.5 + 0.44 * px_[vi] / half
                auto[li, 1] = 0.5 + 0.44 * py_[vi] / half
        uvl.uv.foreach_set("vector", auto.ravel())
        uv_name = AUTO_UV
    pset = set(polys)

    me.calc_loop_triangles()
    nloop = len(me.loops)
    uv = np.empty(nloop * 2, dtype=np.float32)
    uvl.uv.foreach_get("vector", uv)
    uv = uv.reshape(-1, 2)
    nor = np.empty(nloop * 3, dtype=np.float32)
    me.corner_normals.foreach_get("vector", nor)
    nor = nor.reshape(-1, 3)
    co = np.empty(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)

    W = work
    P = np.zeros((W, W, 3), dtype=np.float32)
    N = np.zeros((W, W, 3), dtype=np.float32)
    G = np.zeros((W, W, 3), dtype=np.float32)
    M = np.zeros((W, W), dtype=bool)
    for lt in me.loop_triangles:
        if lt.polygon_index not in pset:
            continue
        l = lt.loops
        t = uv[[l[0], l[1], l[2]]] * W - 0.5
        x0 = max(int(math.floor(t[:, 0].min())), 0)
        x1 = min(int(math.ceil(t[:, 0].max())), W - 1)
        y0 = max(int(math.floor(t[:, 1].min())), 0)
        y1 = min(int(math.ceil(t[:, 1].max())), W - 1)
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        d = (t[1, 1] - t[2, 1]) * (t[0, 0] - t[2, 0]) + (t[2, 0] - t[1, 0]) * (t[0, 1] - t[2, 1])
        if abs(d) < 1e-12:
            continue
        a = ((t[1, 1] - t[2, 1]) * (xs - t[2, 0]) + (t[2, 0] - t[1, 0]) * (ys - t[2, 1])) / d
        b = ((t[2, 1] - t[0, 1]) * (xs - t[2, 0]) + (t[0, 0] - t[2, 0]) * (ys - t[2, 1])) / d
        c = 1.0 - a - b
        e = -0.75 / max(abs(d) ** 0.5, 1.0)  # conservative: also take pixels just outside
        ins = (a >= e) & (b >= e) & (c >= e)
        if not ins.any():
            continue
        v = lt.vertices
        yy, xx = ys[ins], xs[ins]
        w = np.stack((a[ins], b[ins], c[ins]), axis=1).astype(np.float32)
        P[yy, xx] = w @ co[[v[0], v[1], v[2]]]
        N[yy, xx] = w @ nor[[l[0], l[1], l[2]]]
        G[yy, xx] = np.array(lt.normal, dtype=np.float32)
        M[yy, xx] = True

    if not M.any():
        raise RuntimeError("No face polygons found in the UV map")

    idx = np.argwhere(M)
    Pm = P[M]
    Nm = N[M]
    Nm /= np.maximum(np.linalg.norm(Nm, axis=1, keepdims=True), 1e-8)
    Cn = Pm - np.array(center, dtype=np.float32)
    Cn /= np.maximum(np.linalg.norm(Cn, axis=1, keepdims=True), 1e-8)
    r = min(max(roundness, 0.0), 1.0)
    Nr = Nm * (1.0 - r) + Cn * r
    U = np.array(up, dtype=np.float32)
    F = np.array(fwd, dtype=np.float32)
    R = np.array(right, dtype=np.float32)
    Nh = Nr - np.outer(Nr @ U, U)
    Nh /= np.maximum(np.linalg.norm(Nh, axis=1, keepdims=True), 1e-8)
    nF = Nh @ F
    nR = Nh @ R

    bvh = None
    if nose_shadow:
        verts = [v.co.copy() for v in me.vertices]
        tris = [tuple(lt.vertices) for lt in me.loop_triangles]
        inv = obj.matrix_world.inverted()
        for ob in occluders:
            if ob is obj or ob.type != 'MESH':
                continue
            om = ob.data
            om.calc_loop_triangles()
            base = len(verts)
            mat = inv @ ob.matrix_world
            verts.extend(mat @ v.co for v in om.vertices)
            tris.extend(tuple(base + i for i in lt.vertices) for lt in om.loop_triangles)
        bvh = BVHTree.FromPolygons(verts, tris)
    eps = hsize * 0.004
    Gm = G[M]
    org = Pm + Gm * eps
    org_v = [Vector(o) for o in org.tolist()]
    far = hsize * 4.0
    ce, se = math.cos(elevation), math.sin(elevation)

    # Pixels that are already covered with the light straight in front sit inside a cavity
    # (eye sockets, mouth, under the hair line): they follow the smooth shape instead of
    # staying black forever.
    open_px = np.ones(len(Pm), dtype=bool)
    if bvh is not None:
        L0 = (fwd * ce + up * se).normalized()
        for i in range(len(Pm)):
            if bvh.ray_cast(org_v[i], L0, far)[0] is not None:
                open_px[i] = False

    count = np.zeros((2, len(Pm)), dtype=np.float32)
    for side in (0, 1):
        sgn = 1.0 if side == 0 else -1.0
        alive = np.ones(len(Pm), dtype=bool)
        for k in range(steps):
            thr = (k + 0.5) / steps
            ca = 1.0 - 2.0 * thr
            sa = math.sqrt(max(1.0 - ca * ca, 0.0))
            lit = (nF * ca + nR * sa * sgn) > 0.0
            if bvh is not None:
                L = (fwd * ca + right * (sa * sgn)) * ce + up * se
                L.normalize()
                cand = np.flatnonzero(lit & alive & open_px)
                ray = bvh.ray_cast
                for i in cand.tolist():
                    if ray(org_v[i], L, far)[0] is not None:
                        lit[i] = False
            # Once a pixel has gone dark it stays dark: shadows only grow as the light turns.
            alive &= lit
            count[side] += alive
    count /= steps

    out = []
    for side in (0, 1):
        img = np.zeros((W, W), dtype=np.float32)
        img[idx[:, 0], idx[:, 1]] = count[side]
        img, _m = _dilate(img, M, 24)
        mean = float(count[side].mean())
        img[~_m] = mean
        img = _blur(img, smooth * W / 256.0)
        img = _resize(img, size)
        img = _blur(img, 0.6 * size / W)
        out.append(np.clip(img, 0.0, 1.0))

    name = image_name or (obj.name + " Face Map")
    # Blue: where the map can be trusted. The map only describes the front of the head; on the
    # sides and back it fades to 0 and the shader falls back to the Face Shadow node itself.
    valid = np.zeros((W, W), dtype=np.float32)
    valid[idx[:, 0], idx[:, 1]] = np.clip((nF + 0.05) / 0.35, 0.0, 1.0)
    valid, _m = _dilate(valid, M, 3)
    valid = np.clip(_blur(_resize(_blur(valid, 1.0), size), 0.6 * size / W), 0.0, 1.0)
    rgb = np.stack((out[0], out[1], valid), axis=2)
    image = _store_image(name, rgb)
    return image, center, fwd, up, uv_name


def _walk(tree, seen=None):
    seen = seen or set()
    if tree is None or tree.name in seen:
        return
    seen.add(tree.name)
    yield tree
    for n in tree.nodes:
        if n.bl_idname == "ShaderNodeGroup":
            yield from _walk(n.node_tree, seen)


def apply_to_material(mat, image, uv_name, center, fwd, up):
    """Point every Ensō Face Shadow node at the head and read the baked map through Ensō Face Map."""
    done = 0
    for tree in _walk(mat.node_tree):
        for fs in [n for n in tree.nodes if n.bl_idname == "ShaderNodeEnsoFaceShadow"]:
            fs.inputs["Center"].default_value = center
            fs.inputs["Forward"].default_value = fwd
            fs.inputs["Up"].default_value = up
            fm = None
            for l in fs.outputs["Map Threshold"].links:
                if l.to_node.bl_idname == "ShaderNodeEnsoFaceMap":
                    fm = l.to_node
            if fm is None:
                fm = tree.nodes.new("ShaderNodeEnsoFaceMap")
                fm.parent = fs.parent
                fm.location = (fs.location.x + 20, fs.location.y - 420)
                tex = tree.nodes.new("ShaderNodeTexImage")
                tex.parent = fs.parent
                tex.label = "Face Map"
                tex.location = (fs.location.x - 280, fs.location.y - 420)
                uvn = tree.nodes.new("ShaderNodeUVMap")
                uvn.parent = fs.parent
                uvn.location = (fs.location.x - 480, fs.location.y - 420)
                tree.links.new(uvn.outputs[0], tex.inputs["Vector"])
                tree.links.new(tex.outputs["Color"], fm.inputs["Map"])
                tree.links.new(fs.outputs["Map Threshold"], fm.inputs["Threshold"])
                tree.links.new(fs.outputs["Flip"], fm.inputs["Flip"])
                # Blue of the map says where it can be trusted (the front of the face).
                sep = tree.nodes.new("ShaderNodeSeparateColor")
                sep.parent = fs.parent
                sep.location = (fs.location.x + 20, fs.location.y - 640)
                tree.links.new(tex.outputs["Color"], sep.inputs[0])
                mix = tree.nodes.new("ShaderNodeMix")
                mix.data_type = 'FLOAT'
                mix.label = "Map On The Face Only"
                mix.parent = fs.parent
                mix.location = (fs.location.x + 220, fs.location.y - 420)
                users = [l.to_socket for l in fs.outputs["Shade"].links]
                tree.links.new(sep.outputs[2], mix.inputs[0])
                tree.links.new(fs.outputs["Shade"], mix.inputs[2])
                tree.links.new(fm.outputs["Shade"], mix.inputs[3])
                for sock in users:
                    tree.links.new(mix.outputs[0], sock)
            for l in fm.inputs["Map"].links:
                tex = l.from_node
                if tex.bl_idname == "ShaderNodeTexImage":
                    tex.image = image
                    tex.interpolation = 'Cubic'
                    tex.extension = 'EXTEND'
                    for l2 in tex.inputs["Vector"].links:
                        if l2.from_node.bl_idname == "ShaderNodeUVMap":
                            l2.from_node.uv_map = uv_name
            done += 1
    return done


class OBJECT_OT_enso_bake_face_map(Operator):
    """Bake an anime face shadow map from the mesh: no painting, no Cycles. """ \
        """Materials that use Ensō Face Shadow are set up to read it"""
    bl_idname = "object.enso_bake_face_map"
    bl_label = "Bake Ensō Face Map"
    bl_options = {'REGISTER', 'UNDO'}

    group: StringProperty(
        name="Face Group", default="Face",
        description="Vertex group that holds the face. Leave empty to use the whole mesh")
    uv_map: StringProperty(
        name="UV Map", default="",
        description="UV map to bake in. Leave empty and Ensō makes its own, seen from the front "
                    "of the face, so mirrored or overlapping UVs are never a problem")
    size: EnumProperty(
        name="Size", default='1024',
        items=(('512', "512", ""), ('1024', "1024", ""), ('2048', "2048", "")))
    quality: EnumProperty(
        name="Detail", default='256',
        items=(('128', "Fast", "Quick preview"), ('256', "Normal", ""),
               ('384', "High", "Sharper nose and lip shadows, slower")))
    roundness: FloatProperty(
        name="Roundness", default=0.8, min=0.0, max=1.0, subtype='FACTOR',
        description="Higher gives a simpler, cleaner shadow shape. Lower follows the face more")
    nose_shadow: BoolProperty(
        name="Nose Shadow", default=True,
        description="Let the nose, brow and hair cast their shadow into the map")
    elevation: FloatProperty(
        name="Light Height", default=math.radians(20.0), min=0.0, max=math.radians(80.0),
        subtype='ANGLE', description="How high the light sits while the nose shadow is measured")
    smooth: FloatProperty(
        name="Smooth", default=2.5, min=0.0, max=8.0,
        description="Smooths the shadow shapes so the edge is one clean line")
    use_selected: BoolProperty(
        name="Selected Objects Cast Shadow", default=True,
        description="Other selected meshes (hair, hat) also cast their shadow into the map")

    @classmethod
    def poll(cls, context):
        ob = context.active_object
        return ob is not None and ob.type == 'MESH'

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        ob = context.active_object
        if ob.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        occ = [o for o in context.selected_objects if o is not ob] if self.use_selected else []
        try:
            image, center, fwd, up, uv_name = bake_face_map(
                ob, uv_name=self.uv_map, group=self.group, size=int(self.size),
                work=int(self.quality), roundness=self.roundness, elevation=self.elevation,
                nose_shadow=self.nose_shadow, smooth=self.smooth, occluders=occ)
        except RuntimeError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        n = 0
        for slot in ob.material_slots:
            if slot.material and slot.material.node_tree:
                n += apply_to_material(slot.material, image, uv_name, center, fwd, up)
        if n:
            self.report({'INFO'}, "Face map baked and connected to %d Face Shadow node(s)" % n)
            return {'FINISHED'}
        # No Face Shadow node yet: add the whole reading setup to the active material, ready
        # to use, so the map never has to be wired by hand.
        mat = ob.active_material
        if mat is not None and mat.node_tree is not None:
            tree = mat.node_tree
            xs = [nd.location.x for nd in tree.nodes] or [0.0]
            ys = [nd.location.y for nd in tree.nodes] or [0.0]
            frame = tree.nodes.new("NodeFrame")
            frame.label = "Ensō Face Map (use the Shade of Map On The Face Only)"
            fs = tree.nodes.new("ShaderNodeEnsoFaceShadow")
            fs.parent = frame
            fs.location = (min(xs) - 500.0, min(ys) - 200.0)
            n = apply_to_material(mat, image, uv_name, center, fwd, up)
        if n:
            self.report({'INFO'}, "Face map baked and added to \"%s\": plug a light's Direction into "
                        "Ensō Face Shadow and use the Map On The Face Only result as the shade"
                        % mat.name)
        else:
            self.report({'INFO'}, "Face map baked: \"%s\". Give the object a material, then bake "
                        "again or add For Each Light: Anime Face" % image.name)
        return {'FINISHED'}


def _menu(self, context):
    self.layout.separator()
    self.layout.operator(OBJECT_OT_enso_bake_face_map.bl_idname, icon='SHADING_RENDERED')


classes = (OBJECT_OT_enso_bake_face_map,)


def _append_menu():
    # bl_ui registers after bl_operators.
    menu = getattr(bpy.types, "VIEW3D_MT_object", None)
    if menu is None:
        return 0.5
    menu.append(_menu)
    return None


def register():
    # The classes are registered by bl_operators itself.
    bpy.app.timers.register(_append_menu, first_interval=0.1, persistent=True)


def unregister():
    menu = getattr(bpy.types, "VIEW3D_MT_object", None)
    if menu is not None:
        menu.remove(_menu)
