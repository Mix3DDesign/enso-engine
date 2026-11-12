# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō handoff report.

A file made in Ensō opens in regular Blender, but the Ensō nodes in it are unknown there and the
look they make is lost. Before a file is handed to someone else, this lists every Ensō-only node,
the materials and objects that use them and the build that is needed, as a text file next to the
.blend and as a text inside it (so whoever opens the file finds it in the Text Editor). Saving a
file that uses Ensō-only nodes shows a short notice once.
"""

import os

import bpy
from bpy.app.handlers import persistent
from bpy.types import Operator

TEXT_NAME = "READ ME - made with Enso Engine"
_noticed = set()


def _is_enso(node):
    return "Enso" in getattr(node, "bl_idname", "")


def _walk(tree, found, seen):
    if tree is None or tree.name_full in seen:
        return
    seen.add(tree.name_full)
    for node in tree.nodes:
        if _is_enso(node):
            found.append(node)
        sub = getattr(node, "node_tree", None)
        if sub is not None:
            _walk(sub, found, seen)


def collect():
    """Ensō-only nodes by owner: [(kind, name, [nodes], [users])]."""
    rows = []
    users = {}
    for ob in bpy.data.objects:
        for slot in ob.material_slots:
            if slot.material is not None:
                users.setdefault(slot.material.name, []).append(ob.name)
    for mat in bpy.data.materials:
        found = []
        _walk(mat.node_tree, found, set())
        if found:
            rows.append(("Material", mat.name, found, sorted(set(users.get(mat.name, [])))))
    for world in bpy.data.worlds:
        found = []
        _walk(world.node_tree, found, set())
        if found:
            rows.append(("World", world.name, found, []))
    for scene in bpy.data.scenes:
        found = []
        for attr in ("compositing_node_group", "node_tree"):
            _walk(getattr(scene, attr, None), found, set())
        if found:
            rows.append(("Compositor", scene.name, found, []))
    for ob in bpy.data.objects:
        found = []
        for mod in ob.modifiers:
            _walk(getattr(mod, "node_group", None), found, set())
        if found:
            rows.append(("Modifiers of object", ob.name, found, []))
    return rows


def count():
    return sum(len(r[2]) for r in collect())


def build_name():
    name = getattr(bpy.app, "enso_version_string", "") or "Ensō Engine"
    if "Ens" not in name:
        name = "Ensō Engine " + name
    return "%s, based on Blender %d.%d" % (name, bpy.app.version[0], bpy.app.version[1])


def report_text():
    rows = collect()
    total = sum(len(r[2]) for r in rows)
    lines = []
    lines.append("ENSŌ HANDOFF REPORT")
    lines.append("")
    lines.append("File: %s" % (bpy.data.filepath or "(not saved yet)"))
    lines.append("Made with: %s" % build_name())
    lines.append("Get Ensō Engine (free): https://mix3ddesign.com")
    lines.append("")
    if not rows:
        lines.append("This file uses no Ensō-only nodes. It looks the same in regular Blender %d.%d."
                     % (bpy.app.version[0], bpy.app.version[1]))
        return "\n".join(lines) + "\n", 0
    lines.append("WARNING: this file uses %d Ensō-only node(s)." % total)
    lines.append("It opens in regular Blender, and the meshes, rigs, animation, textures and all")
    lines.append("standard nodes are kept. The Ensō nodes show up as undefined there and the look")
    lines.append("they make is lost. Open and render this file in Ensō Engine to get the same result.")
    lines.append("")
    kinds = {}
    for _kind, _name, nodes, _users in rows:
        for node in nodes:
            label = node.bl_label or node.bl_idname
            kinds[label] = kinds.get(label, 0) + 1
    lines.append("ENSŌ-ONLY NODES IN THIS FILE")
    for label in sorted(kinds):
        lines.append("  %3d x %s" % (kinds[label], label))
    lines.append("")
    lines.append("WHERE THEY ARE")
    for kind, name, nodes, users in rows:
        lines.append("%s \"%s\"" % (kind, name))
        seen = {}
        for node in nodes:
            label = node.bl_label or node.bl_idname
            seen[label] = seen.get(label, 0) + 1
        lines.append("    nodes: " + ", ".join(
            "%s x%d" % (k, v) if v > 1 else k for k, v in sorted(seen.items())))
        if kind == "Material":
            lines.append("    used by: " + (", ".join(users) if users else "(no object)"))
    lines.append("")
    return "\n".join(lines) + "\n", total


def write_report(to_disk=True):
    """Make the report text inside the file and, for a saved file, next to it."""
    text, total = report_text()
    block = bpy.data.texts.get(TEXT_NAME)
    if total == 0 and block is None:
        return text, total, ""
    if block is None:
        block = bpy.data.texts.new(TEXT_NAME)
    block.clear()
    block.write(text)
    block.use_fake_user = True
    path = ""
    if to_disk and bpy.data.filepath:
        path = os.path.splitext(bpy.data.filepath)[0] + "_enso_handoff.txt"
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
        except OSError:
            path = ""
    return text, total, path


class WM_OT_enso_handoff_report(Operator):
    """List every Ensō-only node in this file, the materials and objects that use them and """ \
        """the build that is needed, for handing the file to someone else"""
    bl_idname = "wm.enso_handoff_report"
    bl_label = "Ensō Handoff Report"

    def execute(self, context):
        _text, total, path = write_report()
        self._total = total
        self._path = path

        def draw(popup, _context):
            col = popup.layout.column()
            if total == 0:
                col.label(text="No Ensō-only nodes. This file looks the same in regular Blender.",
                          icon='CHECKMARK')
            else:
                col.label(text="%d Ensō-only node(s) in this file." % total, icon='ERROR')
                col.label(text="In regular Blender they are undefined and their look is lost.")
            if path:
                col.label(text="Report saved as: " + os.path.basename(path))
            elif total:
                col.label(text="Save the file to also get the report as a text file next to it.")
            if total:
                col.label(text="It is also in the Text Editor: \"%s\"" % TEXT_NAME)

        context.window_manager.popup_menu(draw, title="Ensō Handoff Report", icon='INFO')
        return {'FINISHED'}


classes = (WM_OT_enso_handoff_report,)


def _menu(self, context):
    self.layout.separator()
    self.layout.operator(WM_OT_enso_handoff_report.bl_idname, icon='TEXT')


def _notice(total):
    def draw(popup, _context):
        col = popup.layout.column()
        col.label(text="This file uses %d Ensō-only node(s)." % total, icon='ERROR')
        col.label(text="It will not look the same in regular Blender.")
        col.label(text="File > Ensō Handoff Report lists them before you hand it over.")

    def show():
        wm = bpy.context.window_manager
        if wm is not None and wm.windows:
            with bpy.context.temp_override(window=wm.windows[0]):
                wm.popup_menu(draw, title="Ensō-only nodes", icon='INFO')
        return None

    bpy.app.timers.register(show, first_interval=0.2)


@persistent
def _on_save(*_args):
    """Keep the report inside the file up to date, and say once that the file is Ensō-only."""
    try:
        _text, total, _path = write_report(to_disk=False)
    except Exception as ex:
        print("Ensō: handoff report failed:", ex)
        return
    if total and not bpy.app.background and bpy.data.filepath not in _noticed:
        _noticed.add(bpy.data.filepath)
        _notice(total)


def _append_menu():
    # bl_ui registers after bl_operators.
    menu = getattr(bpy.types, "TOPBAR_MT_file", None)
    if menu is None:
        return 0.5
    menu.append(_menu)
    return None


def register():
    # The classes are registered by bl_operators itself.
    bpy.app.handlers.save_pre.append(_on_save)
    bpy.app.timers.register(_append_menu, first_interval=0.2, persistent=True)


def unregister():
    if _on_save in bpy.app.handlers.save_pre:
        bpy.app.handlers.save_pre.remove(_on_save)
    menu = getattr(bpy.types, "TOPBAR_MT_file", None)
    if menu is not None:
        menu.remove(_menu)
