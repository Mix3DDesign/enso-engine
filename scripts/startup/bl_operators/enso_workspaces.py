# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō anime workspaces, added in front of the stock tabs of every new file:

  Anime          Image Editor | 3D Viewport | Outliner + Properties   (timeline below)
  Anime Shading  3D Viewport (Material Preview) | Shader Editor | Outliner + Properties

They come from enso_workspaces.blend next to this module's sibling modules folder. Files that
already have them are left alone. "Ensō > Add Anime Workspaces" adds them to any open file.
"""

import bpy
from bpy.app.handlers import persistent

ANIME = "Anime"
SHADING = "Anime Shading"

classes = ()


def _template():
    import os
    for d in bpy.utils.script_paths(subdir="modules"):
        p = os.path.join(d, "enso_workspaces.blend")
        if os.path.isfile(p):
            return p
    return None


def _queue_front(window, names):
    """Put the Ensō tabs first: each is made active, then moved to the front (the operator moves
    the active workspace, and switching takes effect on the next update), ending on Anime."""
    todo = [n for n in names if n in bpy.data.workspaces]

    def step():
        if not todo:
            return None
        name = todo[0]
        ws = bpy.data.workspaces.get(name)
        if ws is None:
            todo.pop(0)
            return 0.01
        if window.workspace != ws:
            window.workspace = ws
            return 0.05
        try:
            with bpy.context.temp_override(window=window, screen=window.screen):
                bpy.ops.workspace.reorder_to_front()
        except Exception as ex:
            print("Ensō: could not move the %s tab to the front:" % name, ex)
        todo.pop(0)
        return 0.01 if todo else None

    bpy.app.timers.register(step, first_interval=0.05)


def add_workspaces(make_active=True):
    """Append the Ensō workspaces from enso_workspaces.blend, the way Blender's own workspace
    templates are added, and put them in front of the stock tabs."""
    wm = bpy.context.window_manager
    path = _template()
    if not wm.windows or path is None:
        return False
    window = wm.windows[0]
    made = False
    for name in (SHADING, ANIME):
        if name in bpy.data.workspaces:
            continue
        try:
            with bpy.context.temp_override(window=window, screen=window.screen):
                bpy.ops.workspace.append_activate(idname=name, filepath=path)
            made = True
        except Exception as ex:
            print("Ensō: could not add the %s workspace:" % name, ex)
    if made or make_active:
        _queue_front(window, [SHADING, ANIME])
    return made


class ENSO_OT_add_anime_workspaces(bpy.types.Operator):
    """Add the Ensō Anime and Anime Shading workspaces to this file"""
    bl_idname = "enso.add_anime_workspaces"
    bl_label = "Add Anime Workspaces"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        if add_workspaces():
            return {'FINISHED'}
        self.report({'INFO'}, "The Anime workspaces are already in this file")
        return {'CANCELLED'}


classes = (ENSO_OT_add_anime_workspaces,)


def apply_anime_color_defaults():
    """Anime look for new files: the Standard view transform (AgX is made for photoreal work and
    washes out flat anime colors)."""
    for scene in bpy.data.scenes:
        vs = scene.view_settings
        try:
            if vs.view_transform == 'AgX':
                vs.view_transform = 'Standard'
                vs.look = 'None'
        except Exception as ex:
            print("Ensō: could not set the Standard view transform:", ex)


def _new_file_timer():
    if bpy.data.filepath == "":
        apply_anime_color_defaults()
    if bpy.app.background:
        return None
    if bpy.data.filepath == "" and ANIME not in bpy.data.workspaces:
        add_workspaces()
    return None


@persistent
def _on_load(*_args):
    if bpy.data.filepath == "":
        apply_anime_color_defaults()
        bpy.app.timers.register(_new_file_timer, first_interval=0.05)


def register():
    bpy.app.handlers.load_post.append(_on_load)
    bpy.app.timers.register(_new_file_timer, first_interval=0.3)


def unregister():
    if _on_load in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(_on_load)
