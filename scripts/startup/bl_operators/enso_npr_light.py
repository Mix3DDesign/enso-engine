# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō NPR Light: per-light settings for drawn-looking lighting.

Each light can be a flat fill, colour its own shadows, and soften or move the edge of its cel
band. The settings are plain custom properties on the light, read by the Ensō For Each Light
presets through the Light attribute type, so they animate and link like any other property and
a light without them simply behaves as before.
"""

import bpy
from bpy.types import Operator, Panel

def _pack():
    """These tools belong to the Ensō NPR Pack: they stay hidden until a pack is installed."""
    try:
        from . import enso_dlc
        return enso_dlc.has_pack()
    except Exception:
        return False


# key: (default, min, max, subtype, label, description)
SETTINGS = {
    "enso_flat": (0.0, 0.0, 1.0, 'FACTOR', "Flat Fill",
                  "The light fills the whole surface evenly, ignoring its shape and shadows: "
                  "an anime fill light"),
    "enso_shadow_amount": (0.0, 0.0, 1.0, 'FACTOR', "Shadow Colour Amount",
                           "How strongly this light paints its shadow side with the shadow colour"),
    "enso_shadow_color": ((0.25, 0.2, 0.45), 0.0, 1.0, 'COLOR', "Shadow Colour",
                          "Colour this light puts in its own shadows"),
    "enso_soft": (0.0, 0.0, 1.0, 'FACTOR', "Edge Softness",
                  "Softens the edge of this light's cel band"),
    "enso_shift": (0.0, -0.5, 0.5, 'NONE', "Band Shift",
                   "Moves the edge of this light's cel band: positive gives more shadow"),
}


def has_settings(light):
    return "enso_flat" in light


def add_settings(light):
    for key, (default, lo, hi, subtype, _label, desc) in SETTINGS.items():
        if key not in light:
            light[key] = default
        ui = light.id_properties_ui(key)
        if subtype == 'COLOR':
            ui.update(default=default, min=lo, max=hi, soft_min=lo, soft_max=hi,
                      subtype='COLOR', description=desc)
        else:
            ui.update(default=default, min=lo, max=hi, soft_min=lo, soft_max=hi,
                      description=desc, **({"subtype": subtype} if subtype != 'NONE' else {}))
        try:
            light.property_overridable_library_set('["%s"]' % key, True)
        except Exception:
            pass


class LIGHT_OT_enso_npr_settings(Operator):
    """Give this light the Ensō NPR settings: flat fill, coloured shadows, edge softness """ \
        """and band shift. They work with the Ensō For Each Light presets"""
    bl_idname = "light.enso_npr_settings"
    bl_label = "Add Ensō NPR Settings"
    bl_options = {'REGISTER', 'UNDO'}

    remove: bpy.props.BoolProperty(default=False, options={'SKIP_SAVE', 'HIDDEN'})

    @classmethod
    def poll(cls, context):
        if not _pack():
            return False
        return getattr(context, "light", None) is not None or (
            context.object is not None and context.object.type == 'LIGHT')

    def execute(self, context):
        light = getattr(context, "light", None) or context.object.data
        if self.remove:
            for key in SETTINGS:
                if key in light:
                    del light[key]
        else:
            add_settings(light)
        light.update_tag()
        for area in context.screen.areas:
            area.tag_redraw()
        return {'FINISHED'}


class DATA_PT_enso_npr_light(Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    bl_label = "Ensō NPR Light"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_EEVEE'}

    @classmethod
    def poll(cls, context):
        if not _pack():
            return False
        return context.light is not None and context.engine in cls.COMPAT_ENGINES

    def draw(self, context):
        layout = self.layout
        light = context.light
        if not has_settings(light):
            layout.operator("light.enso_npr_settings", icon='ADD')
            layout.label(text="For materials using an Ensō For Each Light preset")
            return
        layout.use_property_split = True
        col = layout.column()
        col.prop(light, '["enso_flat"]', text="Flat Fill")
        col.separator()
        col.prop(light, '["enso_shadow_amount"]', text="Shadow Colour")
        sub = col.column()
        sub.active = light["enso_shadow_amount"] > 0.0
        sub.prop(light, '["enso_shadow_color"]', text="Colour")
        col.separator()
        col.prop(light, '["enso_soft"]', text="Edge Softness")
        col.prop(light, '["enso_shift"]', text="Band Shift")
        layout.operator("light.enso_npr_settings", text="Remove", icon='X').remove = True


classes = (
    LIGHT_OT_enso_npr_settings,
    DATA_PT_enso_npr_light,
)
