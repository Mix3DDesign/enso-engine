# SPDX-FileCopyrightText: 2026 Mix 3D Design
# SPDX-License-Identifier: GPL-2.0-or-later
# Ensō Edge Data: geometry nodes group that gives every point of a mesh the place on the nearest
# upward facing sharp edge above it ("anchor"), how far below that edge it is ("drop"), and its
# rest position. The Ensō Ink Bleed node reads these to paint drips that stick to the object.
import bpy

GROUP = ".Ensō Edge Data"
MOD = "Ensō Edge Data"
VERSION = 7


def _set(node, attr, value):
    """Set a node option that may be a property or a menu socket depending on the version."""
    if hasattr(node, attr):
        try:
            setattr(node, attr, value)
            return
        except Exception:
            pass
    for s in node.inputs:
        if s.name.lower() == attr.replace("_", " ").lower() and hasattr(s, "default_value"):
            try:
                s.default_value = value
            except Exception:
                pass


def ensure_group():
    g = bpy.data.node_groups.get(GROUP)
    if g is not None and g.get("enso_edge_data_ver") == VERSION:
        return g
    if g is None:
        g = bpy.data.node_groups.new(GROUP, 'GeometryNodeTree')
    else:
        g.nodes.clear()
        g.interface.clear()
    g["enso_edge_data_ver"] = VERSION
    g.is_modifier = True
    io = g.interface
    io.new_socket("Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    a = io.new_socket("Edge Angle", in_out='INPUT', socket_type='NodeSocketFloat')
    a.default_value = 0.35
    a.subtype = 'ANGLE'
    a.min_value = 0.0
    sp = io.new_socket("Spacing", in_out='INPUT', socket_type='NodeSocketFloat')
    sp.default_value = 0.01
    sp.subtype = 'DISTANCE'
    sp.min_value = 0.0005
    io.new_socket("Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')
    N, L = g.nodes, g.links

    def node(kind, x, y):
        n = N.new(kind)
        n.location = (x, y)
        return n

    gi = node("NodeGroupInput", -1400, 0)
    go = node("NodeGroupOutput", 900, 0)
    pos = node("GeometryNodeInputPosition", -1400, -400)
    # Sharp convex edges that face up (not the underside rim).
    ang = node("GeometryNodeInputMeshEdgeAngle", -1200, 300)
    cmp = node("FunctionNodeCompare", -1000, 300)
    _set(cmp, "data_type", 'FLOAT')
    _set(cmp, "operation", 'GREATER_THAN')
    L.new(ang.outputs["Unsigned Angle"], cmp.inputs[0])
    L.new(gi.outputs["Edge Angle"], cmp.inputs[1])
    nrm = node("GeometryNodeInputNormal", -1200, 150)
    sep_n = node("ShaderNodeSeparateXYZ", -1000, 150)
    L.new(nrm.outputs["Normal"], sep_n.inputs[0])
    up = node("FunctionNodeCompare", -850, 150)
    _set(up, "data_type", 'FLOAT')
    _set(up, "operation", 'GREATER_THAN')
    L.new(sep_n.outputs["Z"], up.inputs[0])
    up.inputs[1].default_value = -0.3
    both = node("FunctionNodeBooleanMath", -700, 250)
    _set(both, "operation", 'AND')
    L.new(cmp.outputs[0], both.inputs[0])
    L.new(up.outputs[0], both.inputs[1])
    # Smooth or bevelled rims have no single sharp edge: there the drips start where the
    # up-facing top ends, i.e. on edges between an up-facing face and one that is not.
    fn = node("GeometryNodeInputNormal", -1200, 520)
    fsep = node("ShaderNodeSeparateXYZ", -1050, 520)
    L.new(fn.outputs["Normal"], fsep.inputs[0])
    fup = node("ShaderNodeMath", -900, 520)
    fup.operation = 'GREATER_THAN'
    L.new(fsep.outputs["Z"], fup.inputs[0])
    fup.inputs[1].default_value = 0.5
    on_face = node("GeometryNodeFieldOnDomain", -750, 520)
    _set(on_face, "data_type", 'FLOAT')
    _set(on_face, "domain", 'FACE')
    L.new(fup.outputs[0], on_face.inputs[0])
    lo = node("ShaderNodeMath", -600, 560)
    lo.operation = 'GREATER_THAN'
    L.new(on_face.outputs[0], lo.inputs[0])
    lo.inputs[1].default_value = 0.01
    hi = node("ShaderNodeMath", -600, 480)
    hi.operation = 'LESS_THAN'
    L.new(on_face.outputs[0], hi.inputs[0])
    hi.inputs[1].default_value = 0.99
    rim = node("ShaderNodeMath", -450, 520)
    rim.operation = 'MULTIPLY'
    L.new(lo.outputs[0], rim.inputs[0])
    L.new(hi.outputs[0], rim.inputs[1])
    rim_b = node("FunctionNodeCompare", -320, 520)
    _set(rim_b, "data_type", 'FLOAT')
    _set(rim_b, "operation", 'GREATER_THAN')
    L.new(rim.outputs[0], rim_b.inputs[0])
    rim_b.inputs[1].default_value = 0.5
    either = node("FunctionNodeBooleanMath", -600, 300)
    _set(either, "operation", 'OR')
    L.new(both.outputs[0], either.inputs[0])
    L.new(rim_b.outputs[0], either.inputs[1])
    # Ink runs down from edges that lie across (not vertical edges along the fall).
    evx = node("GeometryNodeInputMeshEdgeVertices", -1200, 700)
    edir = node("ShaderNodeVectorMath", -1050, 700)
    edir.operation = 'SUBTRACT'
    L.new(evx.outputs["Position 2"], edir.inputs[0])
    L.new(evx.outputs["Position 1"], edir.inputs[1])
    enrm = node("ShaderNodeVectorMath", -900, 700)
    enrm.operation = 'NORMALIZE'
    L.new(edir.outputs["Vector"], enrm.inputs[0])
    esep = node("ShaderNodeSeparateXYZ", -780, 700)
    L.new(enrm.outputs["Vector"], esep.inputs[0])
    eabs = node("ShaderNodeMath", -660, 700)
    eabs.operation = 'ABSOLUTE'
    L.new(esep.outputs["Z"], eabs.inputs[0])
    flat = node("ShaderNodeMath", -540, 700)
    flat.operation = 'LESS_THAN'
    L.new(eabs.outputs[0], flat.inputs[0])
    flat.inputs[1].default_value = 0.7
    flat_b = node("FunctionNodeCompare", -420, 700)
    _set(flat_b, "data_type", 'FLOAT')
    _set(flat_b, "operation", 'GREATER_THAN')
    L.new(flat.outputs[0], flat_b.inputs[0])
    flat_b.inputs[1].default_value = 0.5
    source = node("FunctionNodeBooleanMath", -480, 400)
    _set(source, "operation", 'AND')
    L.new(either.outputs[0], source.inputs[0])
    L.new(flat_b.outputs[0], source.inputs[1])
    m2c = node("GeometryNodeMeshToCurve", -550, 200)
    L.new(gi.outputs["Geometry"], m2c.inputs["Mesh"])
    L.new(source.outputs[0], m2c.inputs["Selection"])
    c2p = node("GeometryNodeCurveToPoints", -380, 200)
    _set(c2p, "mode", 'LENGTH')
    L.new(m2c.outputs["Curve"], c2p.inputs["Curve"])
    L.new(gi.outputs["Spacing"], c2p.inputs["Length"])
    keep = node("GeometryNodeStoreNamedAttribute", -200, 200)
    _set(keep, "data_type", 'FLOAT_VECTOR')
    _set(keep, "domain", 'POINT')
    keep.inputs["Name"].default_value = "enso_anchor_tmp"
    L.new(c2p.outputs["Points"], keep.inputs["Geometry"])
    L.new(pos.outputs["Position"], keep.inputs["Value"])
    # Squash heights so the nearest edge point is the one above, not one off to the side.
    sq = node("ShaderNodeVectorMath", -380, -400)
    sq.operation = 'MULTIPLY'
    L.new(pos.outputs["Position"], sq.inputs[0])
    sq.inputs[1].default_value = (1.0, 1.0, 0.25)
    setp = node("GeometryNodeSetPosition", -20, 200)
    L.new(keep.outputs["Geometry"], setp.inputs["Geometry"])
    L.new(sq.outputs["Vector"], setp.inputs["Position"])
    near = node("GeometryNodeSampleNearest", 150, 0)
    _set(near, "domain", 'POINT')
    L.new(setp.outputs["Geometry"], near.inputs["Geometry"])
    L.new(sq.outputs["Vector"], near.inputs["Sample Position"])
    attr = node("GeometryNodeInputNamedAttribute", 150, -200)
    _set(attr, "data_type", 'FLOAT_VECTOR')
    attr.inputs["Name"].default_value = "enso_anchor_tmp"
    si = node("GeometryNodeSampleIndex", 320, 0)
    _set(si, "data_type", 'FLOAT_VECTOR')
    _set(si, "domain", 'POINT')
    L.new(setp.outputs["Geometry"], si.inputs["Geometry"])
    L.new(attr.outputs["Attribute"], si.inputs["Value"])
    L.new(near.outputs["Index"], si.inputs["Index"])
    sub = node("ShaderNodeVectorMath", 480, -150)
    sub.operation = 'SUBTRACT'
    L.new(si.outputs["Value"], sub.inputs[0])
    L.new(pos.outputs["Position"], sub.inputs[1])
    sep = node("ShaderNodeSeparateXYZ", 620, -150)
    L.new(sub.outputs["Vector"], sep.inputs[0])
    size = node("GeometryNodeAttributeDomainSize", 320, 300)
    _set(size, "component", 'POINTCLOUD')
    L.new(setp.outputs["Geometry"], size.inputs["Geometry"])
    has = node("FunctionNodeCompare", 480, 300)
    _set(has, "data_type", 'INT')
    _set(has, "operation", 'GREATER_THAN')
    L.new(size.outputs["Point Count"], has.inputs[0])
    has.inputs[1].default_value = 0

    def store(geo, name, dtype, value, x):
        s = node("GeometryNodeStoreNamedAttribute", x, 0)
        _set(s, "data_type", dtype)
        _set(s, "domain", 'POINT')
        s.inputs["Name"].default_value = name
        L.new(geo, s.inputs["Geometry"])
        L.new(value, s.inputs["Value"])
        return s.outputs["Geometry"]

    geo = gi.outputs["Geometry"]
    geo = store(geo, "enso_leak_anchor", 'FLOAT_VECTOR', si.outputs["Value"], 450)
    # Points on the up-facing top are covered right up to the rim (no gap above the drips).
    pn = node("GeometryNodeInputNormal", 300, -350)
    pns = node("ShaderNodeSeparateXYZ", 420, -350)
    L.new(pn.outputs["Normal"], pns.inputs[0])
    is_top = node("ShaderNodeMath", 540, -350)
    is_top.operation = 'GREATER_THAN'
    L.new(pns.outputs["Z"], is_top.inputs[0])
    is_top.inputs[1].default_value = 0.5
    neg = node("ShaderNodeMath", 540, -250)
    neg.operation = 'MULTIPLY'
    L.new(sep.outputs["Z"], neg.inputs[0])
    neg.inputs[1].default_value = -1.0
    negc = node("ShaderNodeMath", 660, -250)
    negc.operation = 'MAXIMUM'
    L.new(neg.outputs[0], negc.inputs[0])
    negc.inputs[1].default_value = 0.0
    lift = node("ShaderNodeMath", 780, -300)
    lift.operation = 'MULTIPLY'
    L.new(negc.outputs[0], lift.inputs[0])
    L.new(is_top.outputs[0], lift.inputs[1])
    dropf = node("ShaderNodeMath", 900, -300)
    dropf.operation = 'ADD'
    L.new(sep.outputs["Z"], dropf.inputs[0])
    L.new(lift.outputs[0], dropf.inputs[1])
    geo = store(geo, "enso_leak_drop", 'FLOAT', sep.outputs["Z"], 550)
    geo = store(geo, "enso_leak_rest", 'FLOAT_VECTOR', pos.outputs["Position"], 650)
    geo = store(geo, "enso_leak_valid", 'FLOAT', has.outputs[0], 750)
    # Low-poly meshes: every face also keeps the highest source edge it touches, so the drips
    # are measured exactly per pixel, not only from the corners.
    src_f = node("ShaderNodeMath", 300, 900)
    src_f.operation = 'MULTIPLY'
    L.new(source.outputs[0], src_f.inputs[0])
    src_f.inputs[1].default_value = 1000.0
    emid = node("ShaderNodeVectorMath", 300, 1050)
    emid.operation = 'ADD'
    L.new(evx.outputs["Position 1"], emid.inputs[0])
    L.new(evx.outputs["Position 2"], emid.inputs[1])
    emid_s = node("ShaderNodeSeparateXYZ", 420, 1050)
    L.new(emid.outputs["Vector"], emid_s.inputs[0])
    ew = node("ShaderNodeMath", 540, 950)
    ew.operation = 'MULTIPLY_ADD'
    L.new(emid_s.outputs["Z"], ew.inputs[0])
    ew.inputs[1].default_value = 0.5
    L.new(src_f.outputs[0], ew.inputs[2])
    eoc = node("GeometryNodeEdgesOfCorner", 540, 1150)
    cw = node("GeometryNodeSampleIndex", 700, 1050)
    _set(cw, "data_type", 'FLOAT')
    _set(cw, "domain", 'EDGE')
    L.new(gi.outputs["Geometry"], cw.inputs["Geometry"])
    L.new(ew.outputs[0], cw.inputs["Value"])
    L.new(eoc.outputs["Next Edge Index"], cw.inputs["Index"])
    cof = node("GeometryNodeCornersOfFace", 860, 1050)
    L.new(cw.outputs["Value"], cof.inputs["Weights"])
    cof.inputs["Sort Index"].default_value = -1
    eoc2 = node("GeometryNodeEdgesOfCorner", 1000, 1050)
    L.new(cof.outputs["Corner Index"], eoc2.inputs["Corner Index"])

    def edge_sample(value, dtype, y):
        n = node("GeometryNodeSampleIndex", 1150, y)
        _set(n, "data_type", dtype)
        _set(n, "domain", 'EDGE')
        L.new(gi.outputs["Geometry"], n.inputs["Geometry"])
        L.new(value, n.inputs["Value"])
        L.new(eoc2.outputs["Next Edge Index"], n.inputs["Index"])
        return n.outputs["Value"]

    fa = edge_sample(evx.outputs["Position 1"], 'FLOAT_VECTOR', 1250)
    fb = edge_sample(evx.outputs["Position 2"], 'FLOAT_VECTOR', 1100)
    fw = edge_sample(ew.outputs[0], 'FLOAT', 950)
    fhas = node("ShaderNodeMath", 1300, 950)
    fhas.operation = 'GREATER_THAN'
    L.new(fw, fhas.inputs[0])
    fhas.inputs[1].default_value = 500.0

    def store_face(geo, name, dtype, value, x):
        s_ = node("GeometryNodeStoreNamedAttribute", x, 300)
        _set(s_, "data_type", dtype)
        _set(s_, "domain", 'FACE')
        s_.inputs["Name"].default_value = name
        L.new(geo, s_.inputs["Geometry"])
        L.new(value, s_.inputs["Value"])
        return s_.outputs["Geometry"]

    geo = store_face(geo, "enso_face_a", 'FLOAT_VECTOR', fa, 1400)
    geo = store_face(geo, "enso_face_b", 'FLOAT_VECTOR', fb, 1500)
    geo = store_face(geo, "enso_face_has", 'FLOAT', fhas.outputs[0], 1600)

    # Surface curvature for the Ensō Curvature node: how much the surface bends around each point
    # per meter (positive = convex), softened a little across neighbours.
    # Bend per meter: edge angle divided by edge length (1 / radius of the surface).
    cang = node("GeometryNodeInputMeshEdgeAngle", 650, -450)
    ev = node("GeometryNodeInputMeshEdgeVertices", 650, -600)
    elen = node("ShaderNodeVectorMath", 800, -600)
    elen.operation = 'DISTANCE'
    L.new(ev.outputs["Position 1"], elen.inputs[0])
    L.new(ev.outputs["Position 2"], elen.inputs[1])
    emax = node("ShaderNodeMath", 900, -600)
    emax.operation = 'MAXIMUM'
    L.new(elen.outputs["Value"], emax.inputs[0])
    emax.inputs[1].default_value = 1e-5
    # Hard edges (over about 30 degrees) are left to the node's crisp screen pass; counting them
    # here would smear a corner over whole flat faces of low-poly meshes.
    soft_e = node("ShaderNodeMath", 650, -330)
    soft_e.operation = 'LESS_THAN'
    L.new(cang.outputs["Unsigned Angle"], soft_e.inputs[0])
    soft_e.inputs[1].default_value = 0.52
    cmask = node("ShaderNodeMath", 720, -400)
    cmask.operation = 'MULTIPLY'
    L.new(cang.outputs["Signed Angle"], cmask.inputs[0])
    L.new(soft_e.outputs[0], cmask.inputs[1])
    cnorm = node("ShaderNodeMath", 800, -450)
    cnorm.operation = 'DIVIDE'
    L.new(cmask.outputs[0], cnorm.inputs[0])
    L.new(emax.outputs[0], cnorm.inputs[1])
    on_pt = node("GeometryNodeFieldOnDomain", 950, -450)
    _set(on_pt, "data_type", 'FLOAT')
    _set(on_pt, "domain", 'EDGE')
    L.new(cnorm.outputs[0], on_pt.inputs[0])
    blur = node("GeometryNodeBlurAttribute", 1100, -450)
    _set(blur, "data_type", 'FLOAT')
    L.new(on_pt.outputs[0], blur.inputs[0])
    blur.inputs["Iterations"].default_value = 2
    geo = store(geo, "enso_curv", 'FLOAT', blur.outputs[0], 850)
    one = node("FunctionNodeInputBool", 950, -700)
    one.boolean = True
    geo = store(geo, "enso_data", 'FLOAT', one.outputs[0], 900)
    go.location = (1800, 0)
    L.new(geo, go.inputs["Geometry"])
    return g


def ensure_modifier(ob):
    """Give a mesh the edge data (first in the stack, so the drips ride along with deformation)."""
    if ob is None or ob.type != 'MESH':
        return None
    for m in ob.modifiers:
        if m.type == 'NODES' and m.node_group is not None and m.node_group.name == GROUP:
            return m
    g = ensure_group()
    m = ob.modifiers.new(MOD, 'NODES')
    m.node_group = g
    m.show_expanded = False
    try:
        with bpy.context.temp_override(object=ob, active_object=ob):
            bpy.ops.object.modifier_move_to_index(modifier=m.name, index=0)
    except Exception:
        try:
            ob.modifiers.move(len(ob.modifiers) - 1, 0)
        except Exception:
            pass
    return m
