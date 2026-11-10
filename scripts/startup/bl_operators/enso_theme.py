# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō Engine default look: the Ink & Paper theme (sumi ink panels, paper text, hanko red).

Applied on a fresh install (no saved preferences yet), after Load Factory Preferences, and once
when a new version of the Ensō look ships (ENSO_THEME_VERSION), so Ensō always starts in it. A theme the user picks afterwards is kept, since it
is saved in their preferences like any other theme.
"""

import os
import sys

import bpy
from bpy.app.handlers import persistent

ENSO_THEME_FILE = "Enso_Ink_and_Paper.xml"
ENSO_THEME_VERSION = 7

classes = ()


def _theme_path():
    for d in bpy.utils.preset_paths("interface_theme"):
        p = os.path.join(d, ENSO_THEME_FILE)
        if os.path.isfile(p):
            return p
    return None


def apply_first_run_defaults():
    """Ensō defaults for fresh preferences.
    OpenGL compiles big NPR shaders (Anime Sky, Ensō styles) many times faster than Vulkan
    the first time they are used (measured: 16 s vs 137 s for the Anime Sky). Takes effect on
    the next start; the user can switch it back in Preferences > System."""
    # The free Ensō add-ons that ship with the engine, on by default.
    import addon_utils
    for mod in ("enso_pack_menus", "enso_mv_text", "enso_wiggle_bones"):
        try:
            if not addon_utils.check(mod)[1]:
                addon_utils.enable(mod, default_set=True, persistent=True)
        except Exception as ex:
            print("Ensō: could not enable", mod, ex)
    system = bpy.context.preferences.system
    try:
        if sys.platform == "win32" and system.gpu_backend != 'OPENGL':
            system.gpu_backend = 'OPENGL'
    except Exception as ex:
        print("Ensō: could not set the GPU backend:", ex)


def apply_enso_theme():
    path = _theme_path()
    if path is None or bpy.app.background:
        return None
    apply_first_run_defaults()
    try:
        bpy.ops.script.execute_preset(filepath=path, menu_idname="USERPREF_MT_interface_theme_presets")
        bpy.context.preferences.is_dirty = True    # saved with the preferences
        _mark_theme()
    except Exception as ex:
        print("Ensō: could not apply the default theme:", ex)
    return None


def _marker():
    return os.path.join(bpy.utils.user_resource('CONFIG'), "enso_theme_version.txt")


def _theme_outdated():
    try:
        with open(_marker(), encoding="utf-8") as f:
            return int(f.read().strip() or 0) < ENSO_THEME_VERSION
    except Exception:
        return True


def _mark_theme():
    try:
        os.makedirs(os.path.dirname(_marker()), exist_ok=True)
        with open(_marker(), "w", encoding="utf-8") as f:
            f.write(str(ENSO_THEME_VERSION))
    except Exception as ex:
        print("Ensō: could not save the theme version:", ex)


def _first_run():
    cfg = bpy.utils.user_resource('CONFIG')
    return not os.path.isfile(os.path.join(cfg, "userpref.blend"))


_started = False


def _mark_started():
    global _started
    _started = True
    return None


@persistent
def _factory_prefs_loaded(*_args):
    # Blender also sends this event on every start when the user has no saved startup file,
    # with their saved preferences loaded. Only a real reset counts: the very first run, or
    # Load Factory Preferences once Ensō is up. Otherwise the user's GPU backend (Vulkan) and
    # theme would be put back on every start.
    if _started or _first_run():
        bpy.app.timers.register(apply_enso_theme, first_interval=0.0)


def register():
    bpy.app.handlers.load_factory_preferences_post.append(_factory_prefs_loaded)
    if _first_run() or _theme_outdated():
        bpy.app.timers.register(apply_enso_theme, first_interval=0.0, persistent=True)
    bpy.app.timers.register(_mark_started, first_interval=1.0, persistent=True)


def unregister():
    if _factory_prefs_loaded in bpy.app.handlers.load_factory_preferences_post:
        bpy.app.handlers.load_factory_preferences_post.remove(_factory_prefs_loaded)
