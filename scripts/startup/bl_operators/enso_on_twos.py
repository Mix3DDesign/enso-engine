# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō On Twos: hand-drawn anime timing without baking.

Characters hold each pose for 2, 3 or 4 frames while the camera and everything else stays
smooth. It is done with Blender's own Stepped F-Curve modifier on every animation channel of
the chosen objects, so the keys are untouched, it plays live in the viewport, renders the same
way, and can be removed or changed at any time.
"""

import bpy
from bpy.props import BoolProperty, IntProperty
from bpy.types import Operator, Panel

PROP = "enso_on_twos"


def _fcurves(action):
    """All F-Curves of an action, old and layered style."""
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


def _actions(ob):
    """Actions that move this object: its own, its data's and its shape keys'."""
    ids = [ob, ob.data]
    keys = getattr(ob.data, "shape_keys", None) if ob.data else None
    if keys:
        ids.append(keys)
    out = []
    for idb in ids:
        ad = getattr(idb, "animation_data", None) if idb else None
        if ad is None:
            continue
        if ad.action and ad.action not in out:
            out.append(ad.action)
        for track in ad.nla_tracks:
            for strip in track.strips:
                if strip.action and strip.action not in out:
                    out.append(strip.action)
    return out


def _stepped(fc):
    for m in fc.modifiers:
        if m.type == 'STEPPED':
            return m
    return None


def apply_step(ob, step, offset=0):
    """Put the object on twos (or threes...). step <= 1 removes it. Returns curves touched."""
    n = 0
    for action in _actions(ob):
        for fc in _fcurves(action):
            m = _stepped(fc)
            if step <= 1:
                if m is not None:
                    fc.modifiers.remove(m)
                    n += 1
                continue
            if m is None:
                m = fc.modifiers.new('STEPPED')
            m.frame_step = step
            m.frame_offset = offset
            m.mute = False
            m.show_expanded = False
            n += 1
    if step <= 1:
        if PROP in ob:
            del ob[PROP]
    else:
        ob[PROP] = step
    return n


def set_preview_smooth(scene, smooth):
    """Temporarily play everything smooth (or back on twos) without removing the setup."""
    for ob in scene.objects:
        if PROP not in ob:
            continue
        for action in _actions(ob):
            for fc in _fcurves(action):
                m = _stepped(fc)
                if m is not None:
                    m.mute = smooth


def _targets(context, with_children):
    obs = list(context.selected_objects)
    if context.active_object and context.active_object not in obs:
        obs.append(context.active_object)
    if with_children:
        for ob in list(obs):
            for ch in ob.children_recursive:
                if ch not in obs:
                    obs.append(ch)
    return obs


class ANIM_OT_enso_on_twos(Operator):
    """Hold each pose for several frames, like hand-drawn anime. Nothing is baked: """ \
        """the keys stay as they are and this can be changed or removed at any time"""
    bl_idname = "anim.enso_on_twos"
    bl_label = "Ensō On Twos"
    bl_options = {'REGISTER', 'UNDO'}

    step: IntProperty(
        name="Hold", default=2, min=1, max=12,
        description="Frames each pose is held: 2 is on twos, 3 on threes. 1 is smooth again")
    offset: IntProperty(
        name="Offset", default=0, min=0, max=11,
        description="Shifts which frames the pose changes on, so two characters do not step together")
    children: BoolProperty(
        name="Include Children", default=True,
        description="Also step everything parented to the selection (a rig's meshes, props, hair)")

    @classmethod
    def poll(cls, context):
        return context.active_object is not None or context.selected_objects

    def execute(self, context):
        obs = _targets(context, self.children)
        curves = sum(apply_step(ob, self.step, self.offset) for ob in obs)
        if curves == 0:
            self.report({'WARNING'}, "No animation found on the selection")
            return {'CANCELLED'}
        if self.step <= 1:
            self.report({'INFO'}, "Smooth again: removed from %d channels" % curves)
        else:
            self.report({'INFO'}, "On %ds: %d channels" % (self.step, curves))
        return {'FINISHED'}


class ANIM_OT_enso_on_twos_clear(Operator):
    """Play the selection smooth again"""
    bl_idname = "anim.enso_on_twos_clear"
    bl_label = "Smooth Again"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return context.active_object is not None or context.selected_objects

    def execute(self, context):
        n = sum(apply_step(ob, 1) for ob in _targets(context, True))
        self.report({'INFO'}, "Removed from %d channels" % n)
        return {'FINISHED'}


class ANIM_OT_enso_on_twos_preview(Operator):
    """Switch every stepped object between smooth playback and its held frames, """ \
        """to check the motion underneath. Nothing is removed"""
    bl_idname = "anim.enso_on_twos_preview"
    bl_label = "Preview Smooth"
    bl_options = {'REGISTER', 'UNDO'}

    smooth: BoolProperty(name="Smooth", default=True)

    def execute(self, context):
        set_preview_smooth(context.scene, self.smooth)
        context.scene["enso_on_twos_smooth"] = self.smooth
        return {'FINISHED'}


class OBJECT_PT_enso_on_twos(Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "object"
    bl_label = "Ensō Timing"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return context.object is not None

    def draw(self, context):
        layout = self.layout
        ob = context.object
        cur = ob.get(PROP, 1)
        layout.label(text=("Held for %d frames" % cur) if cur > 1 else "Smooth (on ones)",
                     icon='KEYTYPE_BREAKDOWN_VEC' if cur > 1 else 'KEYTYPE_KEYFRAME_VEC')
        row = layout.row(align=True)
        for n, label in ((2, "Twos"), (3, "Threes"), (4, "Fours")):
            op = row.operator("anim.enso_on_twos", text=label, depress=(cur == n))
            op.step = n
        row.operator("anim.enso_on_twos_clear", text="Smooth", depress=(cur <= 1))
        smooth = bool(context.scene.get("enso_on_twos_smooth", False))
        layout.operator("anim.enso_on_twos_preview",
                        text="Back To Held Frames" if smooth else "Preview Everything Smooth",
                        icon='PLAY').smooth = not smooth


def _menu(self, context):
    self.layout.separator()
    self.layout.operator("anim.enso_on_twos", icon='KEYTYPE_BREAKDOWN_VEC')
    self.layout.operator("anim.enso_on_twos_clear")


classes = (
    ANIM_OT_enso_on_twos,
    ANIM_OT_enso_on_twos_clear,
    ANIM_OT_enso_on_twos_preview,
    OBJECT_PT_enso_on_twos,
)


def _append_menu():
    menu = getattr(bpy.types, "VIEW3D_MT_object_animation", None)
    if menu is None:
        return 0.5
    menu.append(_menu)
    return None


def register():
    # The classes are registered by bl_operators itself.
    bpy.app.timers.register(_append_menu, first_interval=0.1, persistent=True)


def unregister():
    menu = getattr(bpy.types, "VIEW3D_MT_object_animation", None)
    if menu is not None:
        menu.remove(_menu)
