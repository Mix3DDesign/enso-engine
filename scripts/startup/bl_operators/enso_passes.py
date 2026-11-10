# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō Passes: the Ensō shading nodes write their results into render passes by themselves.

Turn the passes on here and every material that uses an Ensō node fills them: the shade, the
cast and form shadows, highlights, rims and lines arrive in the compositor as separate layers,
with nothing to wire in the materials.
"""

import bpy
from bpy.props import BoolProperty
from bpy.types import Operator, Panel

# pass name, the node that fills it, what it holds
PASSES = (
    ("Enso Shade", "Shade Info", "Light and shadow together: 0 in shadow, 1 in light"),
    ("Enso Light", "Shade Info", "How much the surface faces the light, before shadows"),
    ("Enso Cast Shadow", "Shade Info", "Only the shadows other objects throw"),
    ("Enso Form Shadow", "Shade Info", "Only the shadow on the side turned away from the light"),
    ("Enso Cel", "Cel Bands", "The cel bands"),
    ("Enso Face Shade", "Face Shadow / Face Map", "The face shadow shape"),
    ("Enso Hair Ring", "Hair Ring", "The hair highlight ring"),
    ("Enso Highlight", "Stylized Highlight", "The shaped highlight"),
    ("Enso Rim", "Light Rim", "Rim light on the lit side"),
    ("Enso Screen Rim", "Screen Rim", "Screen space rim"),
    ("Enso Line", "Line Detect", "Inside lines and outlines"),
    ("Enso Convex", "Screen Curvature", "Raised edges"),
    ("Enso Concave", "Screen Curvature", "Creases and cavities"),
    ("Enso Mask", "Mask", "The Shape output of the Mask node"),
    ("Enso Flat Color", "Part Tags", "The colour of the surface before any light"),
    ("Enso Face", "Part Tags", "Mask of the face"),
    ("Enso Hair", "Part Tags", "Mask of the hair"),
    ("Enso Eye", "Part Tags", "Mask of the eyes"),
    ("Enso Skin", "Part Tags", "Mask of the skin"),
    ("Enso Cloth", "Part Tags", "Mask of the clothes"),
)

COLOR_PASSES = {"Enso Flat Color"}


def _has(view_layer, name):
    return any(a.name == name for a in view_layer.aovs)


class SCENE_OT_enso_passes_add(Operator):
    """Turn on the Ensō render passes for this view layer. Materials that use Ensō nodes """ \
        """fill them by themselves; they show up on the Render Layers node and in the """ \
        """viewport's render pass list"""
    bl_idname = "scene.enso_passes_add"
    bl_label = "Add Ensō Passes"
    bl_options = {'REGISTER', 'UNDO'}

    only_used: BoolProperty(
        name="Only For Nodes In Use", default=False,
        description="Add only the passes of Ensō nodes that some material in this file uses")

    def execute(self, context):
        vl = context.view_layer
        used = None
        if self.only_used:
            used = set()
            trees = [m.node_tree for m in bpy.data.materials if m.node_tree]
            trees += [g for g in bpy.data.node_groups if g.bl_idname == 'ShaderNodeTree']
            for tree in trees:
                for node in tree.nodes:
                    if node.bl_idname.startswith("ShaderNodeEnso"):
                        used.add(node.bl_label.replace("Ensō ", ""))
        n = 0
        for name, source, _desc in PASSES:
            if _has(vl, name):
                continue
            if used is not None and not any(part.strip() in used for part in source.split("/")):
                continue
            aov = vl.aovs.add()
            aov.name = name
            aov.type = 'COLOR' if name in COLOR_PASSES else 'VALUE'
            n += 1
        self.report({'INFO'}, "%d Ensō passes added" % n if n else "The Ensō passes are already on")
        return {'FINISHED'}


class SCENE_OT_enso_passes_remove(Operator):
    """Remove the Ensō render passes from this view layer"""
    bl_idname = "scene.enso_passes_remove"
    bl_label = "Remove Ensō Passes"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        vl = context.view_layer
        names = {p[0] for p in PASSES}
        for aov in [a for a in vl.aovs if a.name in names]:
            vl.aovs.remove(aov)
        return {'FINISHED'}


class VIEWLAYER_PT_enso_passes(Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "view_layer"
    bl_label = "Ensō Passes"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_EEVEE'}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES

    def draw(self, context):
        layout = self.layout
        vl = context.view_layer
        row = layout.row(align=True)
        row.operator("scene.enso_passes_add", icon='ADD')
        row.operator("scene.enso_passes_remove", text="", icon='X')
        col = layout.column(align=True)
        for name, source, _desc in PASSES:
            on = _has(vl, name)
            r = col.row()
            r.active = on
            r.label(text=name.replace("Enso ", ""), icon='CHECKMARK' if on else 'BLANK1')
            r.label(text=source)


classes = (
    SCENE_OT_enso_passes_add,
    SCENE_OT_enso_passes_remove,
    VIEWLAYER_PT_enso_passes,
)
