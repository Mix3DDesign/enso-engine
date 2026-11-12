# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō hair strand data: what a painted or baked hair map (HM map) holds, taken from the mesh.

Anime hair is modelled as strands, and every strand is its own island in the UV map. For each
island this finds the direction the strand runs in, which end is the root, and how wide the
strand is along its length, and writes:

  "Enso Hair Strand"   UV map: U goes across the strand (0 to 1), V from the root (0) to the tip (1)
  "enso_strand"        one random value per strand
  "enso_strand_around" where around the head the strand line sits (the same value all the way
                       down a strand, so a highlight drawn with it follows the flow of the hair)
  enso_hair_center     the centre of the head, as a property of the object

The Ensō Hair Shadow and Ensō Hair Highlight nodes read these by name. No image is made.
"""

import bpy
from bpy.props import BoolProperty, EnumProperty
from bpy.types import Operator

STRAND_UV = "Enso Hair Strand"
STRAND_ID = "enso_strand"
STRAND_AROUND = "enso_strand_around"
CENTER_PROP = "enso_hair_center"
BINS = 16


def _np():
    import numpy
    return numpy


def _islands(np, me, uv, by_uv):
    """Label of the island every polygon belongs to: polygons joined through shared corners
    (the same vertex at the same UV), or through shared vertices for loose parts."""
    n_loops = len(me.loops)
    n_polys = len(me.polygons)
    loop_vert = np.empty(n_loops, dtype=np.int64)
    me.loops.foreach_get("vertex_index", loop_vert)
    loop_start = np.empty(n_polys, dtype=np.int64)
    loop_total = np.empty(n_polys, dtype=np.int64)
    me.polygons.foreach_get("loop_start", loop_start)
    me.polygons.foreach_get("loop_total", loop_total)
    # Loops of a polygon are stored one after the other.
    first = np.repeat(np.cumsum(loop_total) - loop_total, loop_total)
    idx = np.repeat(loop_start, loop_total) + (np.arange(int(loop_total.sum())) - first)
    loop_poly = np.zeros(n_loops, dtype=np.int64)
    loop_poly[idx] = np.repeat(np.arange(n_polys, dtype=np.int64), loop_total)
    if by_uv:
        q = np.round(uv * 100000.0).astype(np.int64)
        keys = np.stack([loop_vert, q[:, 0], q[:, 1]], axis=1)
        _, group = np.unique(keys, axis=0, return_inverse=True)
    else:
        group = loop_vert
    group = np.asarray(group).reshape(-1)
    n_groups = int(group.max()) + 1 if group.size else 0
    label = np.arange(n_polys, dtype=np.int64)
    for _ in range(4000):
        g = np.full(n_groups, n_polys, dtype=np.int64)
        np.minimum.at(g, group, label[loop_poly])
        new = label.copy()
        np.minimum.at(new, loop_poly, g[group])
        # Jump to the label's own label, so long strands settle in a few rounds.
        new = new[new]
        if np.array_equal(new, label):
            break
        label = new
    return label, loop_poly, loop_vert


def make_strand_data(obj, crown_world, centre_world, by_uv=True, flip=False):
    """Write the strand UV map and the per-strand value. Returns the number of strands."""
    np = _np()
    me = obj.data
    src = None
    for layer in me.uv_layers:
        if layer.name != STRAND_UV and (src is None or layer.active):
            src = layer
    if src is None:
        raise RuntimeError("\"%s\" has no UV map. Unwrap the hair so every strand is its own "
                           "island" % obj.name)
    src_name = src.name
    n_loops = len(me.loops)
    uv = np.empty(n_loops * 2, dtype=np.float32)
    src.uv.foreach_get("vector", uv)
    uv = uv.reshape(-1, 2).astype(np.float64)
    label, loop_poly, loop_vert = _islands(np, me, uv, by_uv)
    co = np.empty(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3).astype(np.float64)
    mw = np.array(obj.matrix_world, dtype=np.float64)
    world = co @ mw[:3, :3].T + mw[:3, 3]
    crown = np.array(crown_world, dtype=np.float64)

    centre_local = (np.array(centre_world, dtype=np.float64) - mw[:3, 3]) @ np.linalg.inv(mw[:3, :3]).T
    out_uv = np.zeros((n_loops, 2), dtype=np.float64)
    out_id = np.zeros(n_loops, dtype=np.float64)
    out_around = np.zeros(n_loops, dtype=np.float64)
    loop_label = label[loop_poly]
    order = np.argsort(loop_label, kind='stable')
    sorted_labels = loop_label[order]
    cuts = np.flatnonzero(np.diff(sorted_labels)) + 1
    groups = np.split(order, cuts)
    centres = (np.arange(BINS) + 0.5) / BINS
    for n, idx in enumerate(groups):
        p = uv[idx]
        mean = p.mean(axis=0)
        d = p - mean
        cov = d.T @ d
        w, vec = np.linalg.eigh(cov)
        major = vec[:, 1]
        minor = vec[:, 0]
        a = d @ major
        c = d @ minor
        span = a.max() - a.min()
        if span < 1e-9:
            out_uv[idx, 0] = 0.5
            out_uv[idx, 1] = 0.5
        else:
            an = (a - a.min()) / span
            # The root is the end nearer the top of the head.
            pos = world[loop_vert[idx]]
            low = an < 0.15
            high = an > 0.85
            if low.any() and high.any():
                d_low = np.linalg.norm(pos[low].mean(axis=0) - crown)
                d_high = np.linalg.norm(pos[high].mean(axis=0) - crown)
                if d_high < d_low:
                    an = 1.0 - an
            if flip:
                an = 1.0 - an
            # Width of the strand along its length, so a strand that narrows to a point still
            # runs from 0 to 1 across.
            b = np.minimum((an * BINS).astype(np.int64), BINS - 1)
            lo = np.full(BINS, np.inf)
            hi = np.full(BINS, -np.inf)
            np.minimum.at(lo, b, c)
            np.maximum.at(hi, b, c)
            valid = np.isfinite(lo)
            if valid.sum() >= 1:
                lo = np.interp(centres, centres[valid], lo[valid])
                hi = np.interp(centres, centres[valid], hi[valid])
            lo_i = np.interp(an, centres, lo)
            hi_i = np.interp(an, centres, hi)
            width = hi_i - lo_i
            ok = width > 1e-9
            cn = np.where(ok, (c - lo_i) / np.where(ok, width, 1.0), 0.5)
            out_uv[idx, 0] = np.clip(cn, 0.0, 1.0)
            out_uv[idx, 1] = an
        # Where around the head each strand line sits, measured in the upper part of the hair
        # where the highlight is, and kept the same all the way down the line.
        rel = co[loop_vert[idx]] - centre_local
        dist = np.linalg.norm(rel, axis=1)
        dist[dist < 1e-9] = 1.0
        down = (1.0 - rel[:, 2] / dist) * 0.5
        az = np.arctan2(rel[:, 1], rel[:, 0])
        sel = (down > 0.12) & (down < 0.5)
        if sel.sum() < 3:
            sel = np.ones(len(idx), dtype=bool)
        mid = np.arctan2(np.sin(az[sel]).mean(), np.cos(az[sel]).mean())
        turn = (az - mid + np.pi) % (2.0 * np.pi) - np.pi
        cn_all = out_uv[idx, 0]
        if np.var(cn_all[sel]) > 1e-6:
            slope, base = np.polyfit(cn_all[sel], turn[sel], 1)
        else:
            slope, base = 0.0, float(turn[sel].mean())
        # Kept unwrapped (and above zero) so a strand on the seam at the back stays in one piece.
        out_around[idx] = (mid + base + slope * cn_all) / (2.0 * np.pi) + 1.5
        # The same strand gets the same value every time the data is made.
        seed = int(idx.min()) * 2654435761 % 4294967296
        out_id[idx] = 0.05 + 0.95 * (((seed ^ (seed >> 13)) * 1274126177 % 4294967296) / 4294967296.0)

    layer = me.uv_layers.get(STRAND_UV)
    if layer is None:
        layer = me.uv_layers.new(name=STRAND_UV, do_init=False)
        # A new layer must not take over as the one that textures and renders use.
        keep = me.uv_layers.get(src_name)
        if keep is not None:
            me.uv_layers.active = keep
            keep.active_render = True
        layer = me.uv_layers.get(STRAND_UV)
    if layer is None:
        raise RuntimeError("\"%s\" has no room for another UV map (8 is the limit)" % obj.name)
    layer.uv.foreach_set("vector", out_uv.astype(np.float32).reshape(-1))
    attr = me.attributes.get(STRAND_ID)
    if attr is not None and (attr.domain != 'CORNER' or attr.data_type != 'FLOAT'):
        me.attributes.remove(attr)
        attr = None
    if attr is None:
        attr = me.attributes.new(STRAND_ID, 'FLOAT', 'CORNER')
    attr.data.foreach_set("value", out_id.astype(np.float32))
    attr = me.attributes.get(STRAND_AROUND)
    if attr is not None and (attr.domain != 'CORNER' or attr.data_type != 'FLOAT'):
        me.attributes.remove(attr)
        attr = None
    if attr is None:
        attr = me.attributes.new(STRAND_AROUND, 'FLOAT', 'CORNER')
    attr.data.foreach_set("value", out_around.astype(np.float32))
    me.update()
    return len(groups)


def head_centre_world(objects):
    """Top of the hair and a guess of the head centre: the hair's own width is close to the
    width of the head, so the centre sits half of that below the top."""
    np = _np()
    pts = []
    for ob in objects:
        me = ob.data
        co = np.empty(len(me.vertices) * 3, dtype=np.float32)
        me.vertices.foreach_get("co", co)
        mw = np.array(ob.matrix_world, dtype=np.float64)
        pts.append(co.reshape(-1, 3).astype(np.float64) @ mw[:3, :3].T + mw[:3, 3])
    p = np.concatenate(pts)
    lo = p.min(axis=0)
    hi = p.max(axis=0)
    mid = (lo + hi) * 0.5
    # The upper third of the hair hugs the head; the lower part can be long and wide.
    top = p[p[:, 2] > hi[2] - (hi[2] - lo[2]) * 0.35]
    radius = 0.5 * min(top[:, 0].max() - top[:, 0].min(), top[:, 1].max() - top[:, 1].min())
    radius = max(min(radius, (hi[2] - lo[2]) * 0.5), 1e-4)
    tmid = (top.min(axis=0) + top.max(axis=0)) * 0.5
    crown = (tmid[0], tmid[1], hi[2])
    centre = (tmid[0], tmid[1], hi[2] - radius)
    return crown, centre


def add_hair_nodes(obj, centre_local, scene):
    """Put the hair nodes in the object's material, wired to each other and ready to use.
    A material that already has an Ensō Hair Shadow node is left alone. Returns True when
    nodes were added."""
    mat = obj.active_material
    fresh = mat is None
    if fresh:
        mat = bpy.data.materials.new("Ensō Hair")
        obj.data.materials.append(mat)
    tree = mat.node_tree
    if tree is None:
        mat.use_nodes = True
        tree = mat.node_tree
    if any(n.bl_idname == "ShaderNodeEnsoHairShadow" for n in tree.nodes):
        return False
    if fresh:
        tree.nodes.clear()
    xs = [n.location.x for n in tree.nodes] or [0.0]
    ys = [n.location.y for n in tree.nodes] or [0.0]
    x0, y0 = min(xs) - 1000.0, min(ys) - 260.0
    frame = tree.nodes.new("NodeFrame")
    frame.label = "Ensō Hair: use Lit for the shadow and Highlight for the marks"

    def add(kind, dx, dy):
        n = tree.nodes.new(kind)
        n.parent = frame
        n.location = (x0 + dx, y0 + dy)
        return n

    light = add("ShaderNodeEnsoLightSelect", 0.0, 0.0)
    lamps = [o for o in scene.objects if o.type == 'LIGHT']
    suns = [o for o in lamps if o.data.type == 'SUN']
    if suns or lamps:
        light.object = (suns or lamps)[0]
    smooth = add("ShaderNodeEnsoSmoothNormals", 0.0, -300.0)
    smooth.inputs["Roundness"].default_value = 1.0
    smooth.inputs["Center"].default_value = centre_local
    shadow = add("ShaderNodeEnsoHairShadow", 280.0, 0.0)
    high = add("ShaderNodeEnsoHairHighlight", 280.0, -420.0)
    links = tree.links
    links.new(light.outputs["Direction"], shadow.inputs["Light Direction"])
    links.new(light.outputs["Direction"], high.inputs["Light Direction"])
    links.new(smooth.outputs["Normal"], shadow.inputs["Normal"])
    links.new(smooth.outputs["Normal"], high.inputs["Normal"])
    if fresh:
        # A new material gets the whole look: shadow colour, base colour and the marks on top.
        base = add("ShaderNodeMix", 560.0, 0.0)
        base.data_type = 'RGBA'
        base.label = "Shadow Colour / Hair Colour"
        base.inputs[6].default_value = (0.13, 0.09, 0.32, 1.0)
        base.inputs[7].default_value = (0.33, 0.28, 0.75, 1.0)
        links.new(shadow.outputs["Lit"], base.inputs[0])
        marks = add("ShaderNodeMix", 780.0, 0.0)
        marks.data_type = 'RGBA'
        marks.blend_type = 'ADD'
        marks.label = "Highlight Colour"
        marks.inputs[7].default_value = (0.75, 0.75, 0.9, 1.0)
        links.new(high.outputs["Highlight"], marks.inputs[0])
        links.new(base.outputs[2], marks.inputs[6])
        emit = add("ShaderNodeEmission", 1000.0, 0.0)
        links.new(marks.outputs[2], emit.inputs["Color"])
        out = add("ShaderNodeOutputMaterial", 1200.0, 0.0)
        links.new(emit.outputs[0], out.inputs["Surface"])
    return True


class OBJECT_OT_enso_hair_strands(Operator):
    """Read the strands of the selected hair from its UV islands and store what the Ensō hair """ \
        """nodes need: where every strand is, how far across it and how far from the root. """ \
        """Replaces a painted or baked hair map"""
    bl_idname = "object.enso_hair_strands"
    bl_label = "Make Ensō Hair Strand Data"
    bl_options = {'REGISTER', 'UNDO'}

    islands: EnumProperty(
        name="Strands From",
        items=(('UV', "UV Islands", "Every island of the active UV map is one strand"),
               ('MESH', "Loose Parts", "Every separate piece of the mesh is one strand")),
        default='UV')
    flip: BoolProperty(
        name="Flip Root And Tip", default=False,
        description="Turn on when the roots come out at the tips")
    add_nodes: BoolProperty(
        name="Add Hair Nodes", default=True,
        description="Also put Ensō Hair Shadow and Ensō Hair Highlight in the hair material, "
                    "wired to a light and to smooth normals. A material that already has them "
                    "is left alone")

    @classmethod
    def poll(cls, context):
        ob = context.active_object
        return ob is not None and ob.type == 'MESH'

    def execute(self, context):
        from mathutils import Vector
        objs = [o for o in context.selected_objects if o.type == 'MESH']
        if context.active_object not in objs:
            objs.append(context.active_object)
        if context.active_object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        crown, centre = head_centre_world(objs)
        total = 0
        added = 0
        try:
            for ob in objs:
                total += make_strand_data(ob, crown, centre, by_uv=(self.islands == 'UV'),
                                          flip=self.flip)
                local = ob.matrix_world.inverted() @ Vector(centre)
                ob[CENTER_PROP] = (local.x, local.y, local.z, 1.0)
                ob.update_tag()
                if self.add_nodes and add_hair_nodes(ob, local, context.scene):
                    added += 1
        except RuntimeError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        if added:
            self.report({'INFO'}, "Hair strand data made for %d strand(s) on %d object(s), and the "
                        "hair nodes were added to the material" % (total, len(objs)))
        else:
            self.report({'INFO'}, "Hair strand data made for %d strand(s) on %d object(s)"
                        % (total, len(objs)))
        return {'FINISHED'}


def _menu(self, context):
    self.layout.operator(OBJECT_OT_enso_hair_strands.bl_idname, icon='CURVES')


classes = (OBJECT_OT_enso_hair_strands,)


def _append_menu():
    # bl_ui registers after bl_operators.
    menu = getattr(bpy.types, "VIEW3D_MT_object", None)
    if menu is None:
        return 0.5
    menu.append(_menu)
    return None


def register():
    # The classes are registered by bl_operators itself.
    bpy.app.timers.register(_append_menu, first_interval=0.15, persistent=True)


def unregister():
    menu = getattr(bpy.types, "VIEW3D_MT_object", None)
    if menu is not None:
        menu.remove(_menu)
