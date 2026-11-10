# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""
Ensō Engine DLC system.

A DLC pack is a ``.ensopack`` file (a zip) containing::

    manifest.json              {"id", "name", "version", "author", "license", "min_engine"}
    library/*.blend            node groups marked as assets, tagged with group["enso_dlc"]
    library/blender_assets.cats.txt

Installed packs live in ``<config>/enso_dlc/<id>/``. Each installed pack is registered as an
asset library (import method: Append), so its nodes show up in the Add menus under their
catalogs (NPR > Shading, NPR > Filters, ...) and are copied into the .blend file when used.
That keeps files openable in regular Blender.

The engine only provides this mechanism. The packs themselves are separate products with
their own license.
"""

import json
import os
import shutil
import zipfile

import bpy
from bpy.props import BoolProperty
from bpy.props import EnumProperty, StringProperty
from bpy.types import Operator, Panel
from bpy.app.handlers import persistent

PACK_EXT = ".ensopack"
LIB_PREFIX = "Ensō DLC: "


def dlc_root():
    return bpy.utils.user_resource('CONFIG', path="enso_dlc", create=True)


def read_manifest(pack_dir):
    try:
        with open(os.path.join(pack_dir, "manifest.json"), encoding="utf-8") as fh:
            data = json.load(fh)
    except Exception:
        return None
    if not isinstance(data, dict) or "id" not in data:
        return None
    return data


def installed_packs():
    root = dlc_root()
    packs = []
    for entry in sorted(os.listdir(root)):
        pack_dir = os.path.join(root, entry)
        if not os.path.isdir(pack_dir):
            continue
        manifest = read_manifest(pack_dir)
        if manifest:
            packs.append((pack_dir, manifest))
    return packs


def _license():
    try:
        import enso_license
        return enso_license
    except Exception as ex:
        print("Ensō DLC: license module missing:", ex)
        return None


def pack_license_state(manifest):
    """None for free packs, else an enso_license.State."""
    lic = _license()
    if lic is None:
        return None if not manifest.get("license_required") and not str(manifest.get("id", "")).endswith("_member") else \
            type("S", (), {"ok": False, "message": "License check unavailable.", "owner": "", "key": "", "kind": ""})()
    if not lic.needs_license(manifest):
        return None
    return lic.status(manifest["id"], manifest)


def active_packs():
    """Installed packs that may load: free packs, and paid packs with a valid license.
    Packs without one stay on disk untouched, they just aren't loaded."""
    out = []
    packs = installed_packs()
    ids = {m["id"] for _d, m in packs}
    for pack_dir, manifest in packs:
        pid = str(manifest["id"])
        # A Patreon edition is skipped when the bought (shop) edition of the same pack is there.
        if pid.endswith("_member") and pid[:-len("_member")] in ids:
            continue
        st = pack_license_state(manifest)
        if st is None or st.ok:
            out.append((pack_dir, manifest))
    return out


_has_pack = None


def has_pack():
    """True when an Ensō pack is installed and unlocked. The pack tools that live in the engine
    (one-node light presets, NPR light settings, Quick Rig, On Twos, scene effects) show only then."""
    global _has_pack
    if _has_pack is None:
        try:
            _has_pack = bool(active_packs())
        except Exception:
            _has_pack = False
    return _has_pack


PACK_STYLES = {'CLEAN', 'FACE', 'HAIR'}
NEEDS_PACK = "This is part of the Ensō NPR Pack"


def sync_asset_libraries():
    """Make sure every active pack is an asset library, and drop libraries of removed or
    unlicensed packs."""
    prefs = bpy.context.preferences
    libs = prefs.filepaths.asset_libraries
    wanted = {}
    for pack_dir, manifest in active_packs():
        wanted[LIB_PREFIX + manifest.get("name", manifest["id"])] = os.path.join(pack_dir, "library")
    # Pack content that ships inside the engine (the scene effects) is its own library, added
    # only while a pack is active.
    try:
        extra = os.path.join(bpy.utils.system_resource('DATAFILES'), "enso_pack")
        if wanted and os.path.isdir(extra):
            wanted[LIB_PREFIX + "Ensō Effects"] = extra
    except Exception:
        pass
    for lib in list(libs):
        if lib.name.startswith(LIB_PREFIX) and lib.name not in wanted:
            libs.remove(lib)
    existing = {lib.name: lib for lib in libs}
    for name, path in wanted.items():
        lib = existing.get(name)
        if lib is None:
            lib = libs.new(name=name, directory=path)
        if lib.path != path:
            lib.path = path
        try:
            lib.import_method = 'APPEND_REUSE'
        except Exception:
            pass


def _builtin_addons():
    """(folder, names) of the add-ons that ship with the engine (scripts/addons_core)."""
    d = bpy.utils.system_resource('SCRIPTS', path="addons_core")
    try:
        names = {os.path.splitext(f)[0] for f in os.listdir(d)} if d else set()
    except Exception:
        names = set()
    return d, names


def _add_pack_path(addons_dir):
    """Put a pack's add-on folder on the module path, after the engine's own add-ons, so an
    add-on the engine ships (e.g. Ensō: Menus) always loads the engine's newer copy even when an
    older pack still contains one with the same name."""
    import sys
    core, _names = _builtin_addons()
    if core and core not in sys.path:
        sys.path.append(core)
    if addons_dir not in sys.path:
        sys.path.append(addons_dir)


def _reload_pack_addon(name, addons_dir):
    """A pack was (re)installed while its add-on is running: load the new files."""
    import sys
    import addon_utils
    mod = sys.modules.get(name)
    if mod is None or not os.path.normcase(getattr(mod, "__file__", "") or "").startswith(os.path.normcase(addons_dir)):
        return
    try:
        addon_utils.disable(name, default_set=False)
    except Exception as ex:
        print("Ensō DLC: could not disable", name, ex)
    for key in [k for k in list(sys.modules) if k == name or k.startswith(name + ".")]:
        del sys.modules[key]


def enable_pack_addons(reload=False):
    """Packs can ship runtime add-ons (e.g. the Ensō NPR panel and its live light tracking).
    They are enabled from the pack folder unless the same add-on is already installed by the
    user. reload=True (after installing a pack) loads the new files of add-ons already running."""
    import sys
    import addon_utils
    active = active_packs()
    active_dirs = {d for d, _m in active}
    builtin = _builtin_addons()[1]
    for pack_dir, manifest in installed_packs():
        if pack_dir in active_dirs:
            continue
        # No valid license: make sure none of its add-ons keep running.
        for name in manifest.get("addons") or []:
            mod = sys.modules.get(name)
            if name in builtin or mod is None:
                continue
            if os.path.normcase(getattr(mod, "__file__", "") or "").startswith(os.path.normcase(pack_dir)):
                try:
                    addon_utils.disable(name, default_set=True)
                except Exception as ex:
                    print("Ensō DLC: could not disable", name, ex)
        addons_dir = os.path.join(pack_dir, "addons")
        if addons_dir in sys.path:
            sys.path.remove(addons_dir)
    for pack_dir, manifest in active:
        names = manifest.get("addons") or []
        if not names:
            continue
        addons_dir = os.path.join(pack_dir, "addons")
        if not os.path.isdir(addons_dir):
            continue
        _add_pack_path(addons_dir)
        names = [n for n in names if n not in builtin]
        if reload:
            for name in names:
                _reload_pack_addon(name, addons_dir)
        enabled = {m for m in bpy.context.preferences.addons.keys()}
        for name in names:
            if any(m == name or m.endswith("." + name) for m in enabled) and name in sys.modules:
                continue
            try:
                addon_utils.enable(name, default_set=True, persistent=True)
            except Exception as ex:
                print("Ensō DLC: could not enable", name, ex)


# ---- built-in light link for light-driven Ensō nodes ---------------------------------
# Light-driven Ensō nodes read their lights from small value nodes inside the node
# (labelled "L1 X", "L1 Sun", "L1 On", ...). The engine fills them itself from the scene
# lights (suns first, then the strongest), so a node works the moment it is added, with no
# panel, button or add-on needed. Lights picked in the Ensō NPR add-on panel (when that
# add-on is on) take priority, in the same slots.

_LIGHT_LABELS = ("X", "Y", "Z", "Sun", "On", "Col", "Str")
_node_cache = {}


def _light_value_nodes(g):
    ent = _node_cache.get(g.name)
    if ent is not None and all(k in g.nodes for k in ent.values()):
        return ent
    ent = {}
    for n in g.nodes:
        lab = getattr(n, "label", "")
        if lab[:1] == "L" and lab[1:2].isdigit() and n.bl_idname in ("ShaderNodeValue", "ShaderNodeRGB"):
            ent[lab] = n.name
    _node_cache[g.name] = ent
    return ent


def _light_hidden(ob):
    try:
        return ob.hide_viewport or ob.hide_render or bool(ob.hide_get())
    except Exception:
        return bool(getattr(ob, "hide_render", False))


def _scene_auto_lights(scene):
    lights = [o for o in scene.objects if o.type == 'LIGHT' and not _light_hidden(o)]
    lights.sort(key=lambda o: (0 if o.data.type == 'SUN' else 1, -float(getattr(o.data, "energy", 0.0))))
    return (lights + [None, None, None])[:3]


def _light_values(ob):
    """Same as the Ensō add-on: sun -> world +Z direction, other lights -> world position."""
    from mathutils import Vector
    c = ob.data.color
    e = float(getattr(ob.data, "energy", 1.0))
    sun = ob.data.type == 'SUN'
    strength = max(0.0, min(e if sun else e / 100.0, 10.0))
    if sun:
        d = ob.matrix_world.to_3x3() @ Vector((0.0, 0.0, 1.0))
        d.normalize()
        return (d.x, d.y, d.z, 1.0, (c[0], c[1], c[2]), strength)
    p = ob.matrix_world.translation
    return (p.x, p.y, p.z, 0.0, (c[0], c[1], c[2]), strength)


def _set(node, val):
    try:
        out = node.outputs[0]
        if isinstance(val, tuple):
            cur = out.default_value
            if sum(abs(cur[k] - val[k]) for k in range(3)) > 1e-6:
                out.default_value = (val[0], val[1], val[2], 1.0)
        elif abs(out.default_value - val) > 1e-6:
            out.default_value = val
    except Exception:
        pass


def _picked_lights(scene):
    """Lights the user picked in the Ensō add-on panel (if that add-on is on), else None."""
    props = getattr(scene, "enso", None)
    if props is None:
        return None
    picked = [getattr(props, s, None) for s in ("key_light", "key_light2", "key_light3")]
    if not any(picked):
        return None
    valid = [o if (o is not None and scene.objects.get(o.name) == o and not _light_hidden(o)) else None
             for o in picked]
    return valid if any(valid) else None   # stale picks (deleted / hidden lights): use the scene lights


def update_enso_lights(scene):
    groups = [g for g in bpy.data.node_groups if g.bl_idname == 'ShaderNodeTree' and g.get("enso_lights")]
    if not groups or scene is None:
        return
    lights = _picked_lights(scene)
    if lights is None:
        lights = _scene_auto_lights(scene)
        # The Ensō NPR add-on (any version), when enabled, also writes these nodes from its
        # own light pickers and switches the lights off while they are empty. Fill its
        # pickers with the same lights so both agree.
        props = getattr(scene, "enso", None)
        if props is not None and any(lights):
            for slot, ob in zip(("key_light", "key_light2", "key_light3"), lights):
                try:
                    setattr(props, slot, ob)
                except Exception:
                    pass
    for g in groups:
        ent = _light_value_nodes(g)
        for i, ob in enumerate(lights, start=1):
            nodes = {k: g.nodes.get(ent.get("L%d %s" % (i, k), "")) for k in _LIGHT_LABELS}
            if nodes["On"] is None:
                continue
            if ob is None:
                _set(nodes["On"], 0.0)
                continue
            x, y, z, sun, col, strength = _light_values(ob)
            for k, v in (("X", x), ("Y", y), ("Z", z), ("Sun", sun), ("On", 1.0), ("Str", strength)):
                if nodes[k] is not None:
                    _set(nodes[k], v)
            if nodes["Col"] is not None:
                _set(nodes["Col"], col)


@persistent
def _enso_lights_handler(scene, _depsgraph=None):
    if not isinstance(scene, bpy.types.Scene):
        scene = bpy.context.scene
    try:
        update_enso_lights(scene)
    except Exception as ex:
        print("Ensō: light update failed:", ex)


def _enso_lights_timer():
    try:
        update_enso_lights(bpy.context.scene)
    except Exception:
        pass
    return None


def refresh_packs(reload=False):
    global _has_pack
    _has_pack = None
    try:
        sync_asset_libraries()
        enable_pack_addons(reload=reload)
    except Exception as ex:
        print("Ensō DLC: could not register packs:", ex)
    _redraw_prefs()


def _redraw_prefs():
    try:
        for win in bpy.context.window_manager.windows:
            for area in win.screen.areas:
                if area.type == 'PREFERENCES':
                    area.tag_redraw()
    except Exception:
        pass


_license_notice = {}


def _on_license_blocked(pack_id, message):
    if message:
        _license_notice[pack_id] = message
        print("Ensō DLC:", pack_id, message)
    else:
        _license_notice.pop(pack_id, None)
    refresh_packs(reload=True)


def _renew_licenses():
    """Renew pack licenses in the background (once a day), then re-check every 6 hours."""
    lic = _license()
    if lic is not None and not bpy.app.background:
        ids = [m["id"] for _d, m in installed_packs() if lic.needs_license(m)]
        if ids:
            lic.renew_in_background(ids, _on_license_blocked)
    return 6 * 3600.0


@persistent
def _enso_dlc_load_post(_):
    refresh_packs()
    bpy.app.timers.register(_enso_lights_timer, first_interval=0.1)


def _safe_members(zf):
    for member in zf.infolist():
        name = member.filename.replace("\\", "/")
        if name.startswith("/") or ".." in name.split("/"):
            raise ValueError("unsafe path in pack: " + member.filename)
        yield member


class PREFERENCES_OT_enso_dlc_install(Operator):
    """Install an Ensō DLC pack (.ensopack)"""
    bl_idname = "preferences.enso_dlc_install"
    bl_label = "Install Ensō DLC"
    bl_options = {'INTERNAL'}

    filepath: StringProperty(subtype='FILE_PATH')
    filter_glob: StringProperty(default="*" + PACK_EXT, options={'HIDDEN'})

    def invoke(self, context, _event):
        context.window_manager.fileselect_add(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        if not self.filepath.lower().endswith(PACK_EXT) or not os.path.isfile(self.filepath):
            self.report({'ERROR'}, "Pick a " + PACK_EXT + " file")
            return {'CANCELLED'}
        try:
            with zipfile.ZipFile(self.filepath) as zf:
                manifest = json.loads(zf.read("manifest.json").decode("utf-8"))
                pack_id = "".join(c for c in str(manifest["id"]) if c.isalnum() or c in "_-")
                if not pack_id:
                    raise ValueError("pack id is empty")
                target = os.path.join(dlc_root(), pack_id)
                old = read_manifest(target) if os.path.isdir(target) else None
                if os.path.isdir(target):
                    shutil.rmtree(target)
                os.makedirs(target)
                zf.extractall(target, members=list(_safe_members(zf)))
        except Exception as ex:
            self.report({'ERROR'}, "Could not install pack: {:s}".format(str(ex)))
            return {'CANCELLED'}
        # Add-ons the new version no longer ships are turned off.
        if old:
            import addon_utils
            for name in set(old.get("addons") or []) - set(manifest.get("addons") or []):
                try:
                    addon_utils.disable(name, default_set=True)
                except Exception:
                    pass
        refresh_packs(reload=True)
        context.preferences.is_dirty = True
        st = pack_license_state(manifest)
        extra = "" if st is None or st.ok else ". Enter your license key below to unlock it"
        self.report({'INFO'}, "Installed {:s} {:s}{:s}".format(manifest.get("name", pack_id),
                                                              str(manifest.get("version", "")), extra))
        return {'FINISHED'}


class PREFERENCES_OT_enso_dlc_remove(Operator):
    """Remove an installed Ensō DLC pack"""
    bl_idname = "preferences.enso_dlc_remove"
    bl_label = "Remove Ensō DLC"
    bl_options = {'INTERNAL'}

    pack_id: StringProperty()

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event)

    def execute(self, context):
        target = os.path.join(dlc_root(), self.pack_id)
        if os.path.isdir(target) and os.path.normpath(os.path.dirname(target)) == os.path.normpath(dlc_root()):
            shutil.rmtree(target)
        sync_asset_libraries()
        context.preferences.is_dirty = True
        return {'FINISHED'}


def _online_ok(op):
    if getattr(bpy.app, "online_access", True):
        return True
    op.report({'ERROR'}, "Online access is off. Turn on Preferences > System > Network > Allow Online Access")
    return False


class PREFERENCES_OT_enso_license_activate(Operator):
    """Unlock this pack with your license key (from the shop or from Patreon)"""
    bl_idname = "preferences.enso_license_activate"
    bl_label = "Activate"
    bl_options = {'INTERNAL'}

    pack_id: StringProperty()

    def execute(self, context):
        lic = _license()
        key = context.window_manager.enso_license_key.strip()
        if lic is None or not key:
            self.report({'ERROR'}, "Paste your license key first")
            return {'CANCELLED'}
        if not _online_ok(self):
            return {'CANCELLED'}
        lic.forget_block(self.pack_id)
        ok, msg = lic.activate(self.pack_id, key)
        if not ok:
            self.report({'ERROR'}, msg)
            return {'CANCELLED'}
        _license_notice.pop(self.pack_id, None)
        context.window_manager.enso_license_key = ""
        refresh_packs(reload=True)
        self.report({'INFO'}, msg)
        return {'FINISHED'}


class PREFERENCES_OT_enso_license_deactivate(Operator):
    """Remove the license from this computer, to use it on another one. The pack stays installed"""
    bl_idname = "preferences.enso_license_deactivate"
    bl_label = "Deactivate on This Computer"
    bl_options = {'INTERNAL'}

    pack_id: StringProperty()

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event)

    def execute(self, context):
        lic = _license()
        if lic is not None:
            lic.deactivate(self.pack_id)
        refresh_packs()
        return {'FINISHED'}


class PREFERENCES_OT_enso_license_page(Operator):
    """Open the Ensō license page in your browser"""
    bl_idname = "preferences.enso_license_page"
    bl_label = "Open License Page"
    bl_options = {'INTERNAL'}

    page: StringProperty(default="/")

    def execute(self, context):
        lic = _license()
        if lic is not None:
            bpy.ops.wm.url_open(url=lic.SERVER + self.page)
        return {'FINISHED'}


class NODE_OT_enso_unlock(Operator):
    """Open this Ensō node up as a regular node group you can edit"""
    bl_idname = "node.enso_unlock"
    bl_label = "Unlock Ensō Node"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        node = getattr(context, "active_node", None)
        tree = getattr(node, "node_tree", None)
        return tree is not None and "enso_dlc" in tree and "enso_unlocked" not in tree

    def execute(self, context):
        context.active_node.node_tree["enso_unlocked"] = True
        return {'FINISHED'}


class USERPREF_PT_enso_dlc(Panel):
    bl_space_type = 'PREFERENCES'
    bl_region_type = 'WINDOW'
    bl_context = "addons"
    bl_label = "Ensō DLC Packs"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout
        layout.operator("preferences.enso_dlc_install", icon='IMPORT')
        packs = installed_packs()
        if not packs:
            layout.label(text="No packs installed yet.")
            return
        for _pack_dir, manifest in packs:
            box = layout.box()
            row = box.row()
            row.label(text="{:s}  {:s}".format(manifest.get("name", manifest["id"]),
                                                str(manifest.get("version", ""))), icon='PACKAGE')
            row.operator("preferences.enso_dlc_remove", text="", icon='TRASH').pack_id = manifest["id"]
            if manifest.get("author"):
                box.label(text="by " + manifest["author"])
            st = pack_license_state(manifest)
            if st is None:
                continue
            pid = manifest["id"]
            if st.ok:
                who = st.owner or "you"
                col = box.column(align=True)
                col.label(text="Licensed to {:s}".format(who), icon='CHECKMARK')
                if st.key:
                    col.label(text="License " + st.key[:10] + "…" + st.key[-5:], icon='BLANK1')
                box.operator("preferences.enso_license_deactivate", icon='UNLINKED').pack_id = pid
                continue
            col = box.column()
            msg = _license_notice.get(pid) or st.message
            col.label(text=msg, icon='LOCKED')
            row = col.row(align=True)
            row.prop(context.window_manager, "enso_license_key", text="Key")
            row.operator("preferences.enso_license_activate", icon='KEY_HLT').pack_id = pid
            col.operator("preferences.enso_license_page", text="Patreon Member? Log in with Patreon to Get Your Key",
                         icon='URL').page = "/patreon/login"
            col.operator("preferences.enso_license_page", text="Privacy", icon='INFO',
                         emboss=False).page = "/privacy"


def draw_node_context_menu(self, context):
    if NODE_OT_enso_unlock.poll(context):
        self.layout.separator()
        self.layout.operator("node.enso_unlock", icon='UNLOCKED')


class NODE_OT_enso_add_for_each_light(Operator):
    """Add a For Each Light setup: the Light Accumulation node runs its inputs once per scene \
light (EEVEE) and adds the results, so every light gets its own cel shading in its own color"""
    bl_idname = "node.enso_add_for_each_light"
    bl_label = "For Each Light"
    bl_options = {'REGISTER', 'UNDO'}

    style: EnumProperty(
        name="Style",
        items=(
            ('CEL', "Cel", "Light factor times light power, banded by a constant ramp, in the light color"),
            ('CEL_SPEC', "Cel + Highlight", "Cel bands plus a sharp toon highlight from each light"),
            ('CEL_RIM', "Cel + Rim", "Cel bands plus a rim of light on the edges facing away, per light"),
            ('SOFT', "Soft Bands", "Three soft painted bands with spot light cutoff smoothing"),
            ('TINTED', "Tinted Shadows", "Each light paints its color into the lit side and a tinted color into its shadow side"),
            ('HALFTONE', "Halftone", "Comic halftone dots that grow with the light"),
            ('CLEAN', "Clean Cel", "Rounded normals and pixel-smooth cel bands: clean shadows on any mesh"),
            ('FACE', "Anime Face", "Face shadow that sweeps left and right with each light, no painting or baking"),
            ('HAIR', "Anime Hair", "Tube normals for clean bands on hair plus the zigzag ring highlight"),
        ),
        default='CEL',
    )
    single_node: BoolProperty(
        name="Single Node",
        description="Wrap the setup in one node with a Base Color input. Press Tab on it to open "
                    "and change the nodes inside. Turn off to get the loose nodes in a frame",
        default=True,
    )

    @classmethod
    def poll(cls, context):
        space = context.space_data
        return (space is not None and space.type == 'NODE_EDITOR' and space.edit_tree is not None
                and space.edit_tree.bl_idname == 'ShaderNodeTree')

    def execute(self, context):
        if self.style in PACK_STYLES and not has_pack():
            self.report({'ERROR'}, NEEDS_PACK)
            return {'CANCELLED'}
        tree = context.space_data.edit_tree
        for n in tree.nodes:
            n.select = False
        x, y = context.space_data.cursor_location
        label = bpy.types.UILayout.enum_item_name(self, "style", self.style)
        if self.single_node:
            node = build_for_each_light_group(tree, self.style, (x, y), label)
            tree.nodes.active = node
        else:
            build_for_each_light(tree, self.style, (x, y), label)
        return {'FINISHED'}

    def invoke(self, context, event):
        result = self.execute(context)
        if 'FINISHED' in result:
            bpy.ops.node.translate_attach_remove_on_cancel('INVOKE_DEFAULT')
        return result


def build_for_each_light_group(tree, style='CEL', location=(0.0, 0.0), label="Cel"):
    """The For Each Light setup as one node: a group with Base Color in and Shader out."""
    group = bpy.data.node_groups.new("Ensō Lights: " + label, "ShaderNodeTree")
    group.interface.new_socket("Shader", in_out='OUTPUT', socket_type='NodeSocketShader')
    col = group.interface.new_socket("Base Color", in_out='INPUT', socket_type='NodeSocketColor')
    col.default_value = (0.8, 0.8, 0.8, 1.0)
    col.description = "Colour of the surface where the lights reach it"
    hi = group.interface.new_socket("Highlight Color", in_out='INPUT', socket_type='NodeSocketColor')
    hi.default_value = (1.0, 1.0, 1.0, 1.0)
    hi.description = "Colour of highlights, for the styles that have them"
    acc = build_for_each_light(group, style, (0.0, 0.0), label)
    gin = group.nodes.new("NodeGroupInput")
    gin.location = (acc.location.x - 220.0, acc.location.y - 260.0)
    gout = group.nodes.new("NodeGroupOutput")
    gout.location = (acc.location.x + 260.0, acc.location.y)
    # The base colour also goes to the Flat Color pass, and face and hair styles tag their part.
    try:
        tags = group.nodes.new("ShaderNodeEnsoPartTags")
        tags.location = (acc.location.x - 220.0, acc.location.y - 120.0)
        tags.hide = True
        if style == 'FACE':
            tags.inputs["Face"].default_value = 1.0
        elif style == 'HAIR':
            tags.inputs["Hair"].default_value = 1.0
        group.links.new(gin.outputs["Base Color"], tags.inputs["Color"])
        group.links.new(tags.outputs["Color"], acc.inputs["Diffuse Color"])
    except RuntimeError:
        group.links.new(gin.outputs["Base Color"], acc.inputs["Diffuse Color"])
    group.links.new(gin.outputs["Highlight Color"], acc.inputs["Glossy Color"])
    group.links.new(acc.outputs[0], gout.inputs[0])
    for n in group.nodes:
        n.select = False
    node = tree.nodes.new("ShaderNodeGroup")
    node.node_tree = group
    node.location = location
    node.width = 220.0
    node.select = True
    return node


def build_for_each_light(tree, style='CEL', location=(0.0, 0.0), label="Cel"):
    """Build a For Each Light node setup in a shader node tree (used by the operator)."""
    x, y = location
    nodes, links = tree.nodes, tree.links
    frame = nodes.new("NodeFrame")
    frame.label = "For Each Light"
    if style != 'CEL':
        frame.label = "For Each Light: " + label
    frame.use_custom_color = True
    frame.color = (0.25, 0.18, 0.08)

    def add(kind, dx, dy, label=None):
        n = nodes.new(kind)
        n.location = (x + dx, y + dy)
        n.parent = frame
        n.select = True
        if label:
            n.label = label
        return n

    def math(op, dx, dy, a=None, b=None, label=None):
        m = add("ShaderNodeMath", dx, dy, label)
        m.operation = op
        for i, v in enumerate((a, b)):
            if v is None:
                continue
            if isinstance(v, (int, float)):
                m.inputs[i].default_value = v
            else:
                links.new(v, m.inputs[i])
        return m.outputs[0]

    def ramp(fac, dx, dy, stops, interp='CONSTANT', label=None):
        r = add("ShaderNodeValToRGB", dx, dy, label)
        cr = r.color_ramp
        cr.interpolation = interp
        els = cr.elements
        while len(els) < len(stops):
            els.new(0.5)
        for e, (pos, val) in zip(els, stops):
            e.position = pos
            e.color = val if len(val) == 4 else (val[0], val[1], val[2], 1.0)
        links.new(fac, r.inputs["Fac"])
        return r.outputs["Color"]

    def mul_color(a, b, dx, dy, label=None):
        m = add("ShaderNodeMix", dx, dy, label)
        m.data_type = 'RGBA'
        m.blend_type = 'MULTIPLY'
        m.clamp_factor = True
        m.inputs[0].default_value = 1.0
        for i, v in ((6, a), (7, b)):
            if isinstance(v, tuple):
                m.inputs[i].default_value = v
            else:
                links.new(v, m.inputs[i])
        return m.outputs[2]

    def add_color(a, b, dx, dy, label=None):
        m = add("ShaderNodeMix", dx, dy, label)
        m.data_type = 'RGBA'
        m.blend_type = 'ADD'
        m.inputs[0].default_value = 1.0
        links.new(a, m.inputs[6])
        links.new(b, m.inputs[7])
        return m.outputs[2]

    info = add("ShaderNodeLightInfo", -900, 180)
    ev = add("ShaderNodeLightEvaluation", -1100, -60)
    acc = add("ShaderNodeLightAccumulation", 300, 0)
    # The light's strength on this point: shading factor times light power.
    lit = math('MULTIPLY', -880, -60, ev.outputs["Factor"], info.outputs["Power"], "Light Amount")
    gray = (0.25, 0.25, 0.25, 1.0)
    diffuse = None

    if style == 'SOFT':
        lit = math('MULTIPLY', -700, -160, lit, ev.outputs["Mask"], "Spot Cutoff")
        bands = ramp(lit, -500, -60, ((0.0, (0, 0, 0)), (0.12, (0.35, 0.35, 0.35)), (0.45, (1, 1, 1))),
                     'EASE', "Bands")
        diffuse = mul_color(info.outputs["Color"], bands, -150, 60, "Light Color")
    elif style == 'TINTED':
        band = ramp(lit, -620, -60, ((0.0, (0, 0, 0)), (0.2, (1, 1, 1))), 'CONSTANT', "Light / Shadow")
        tint = add("ShaderNodeMix", -300, 40, "Shadow Tint")
        tint.data_type = 'RGBA'
        tint.blend_type = 'MIX'
        links.new(band, tint.inputs[0])
        tint.inputs[6].default_value = (0.08, 0.05, 0.16, 1.0)
        links.new(info.outputs["Color"], tint.inputs[7])
        diffuse = tint.outputs[2]
    elif style in {'CLEAN', 'FACE', 'HAIR'}:
        # Ensō clean cel: smooth normals feed the light, Cel Bands cut it with a pixel-smooth edge.
        cel = add("ShaderNodeEnsoCelBands", -560, -60)
        if style == 'FACE':
            face = add("ShaderNodeEnsoFaceShadow", -840, -60)
            face.inputs["Roundness"].default_value = 0.9
            links.new(ev.outputs["Direction"], face.inputs["Light Direction"])
            cel.location = (x - 560, y + 200)
            # The face shadow already has a smooth edge; the light's own shadows and spot
            # cutoff still apply through the mask.
            # Real cast shadows put small dark shapes in the eye sockets and under the nose,
            # so they are off by default. Raise Cast Shadows to let hair and hats shade the face.
            cast = add("ShaderNodeMix", -760, -300, "Cast Shadows")
            cast.data_type = 'FLOAT'
            cast.inputs[0].default_value = 0.0
            cast.inputs[2].default_value = 1.0
            links.new(ev.outputs["Mask"], cast.inputs[3])
            shade = math('MULTIPLY', -560, -60, face.outputs["Shade"], cast.outputs[0], "Face Shade")
            nodes.remove(cel)
            nodes.remove(lit.node)
            lit_col = mul_color(info.outputs["Color"], shade, -150, 60, "Light Color")
            diffuse = lit_col
        else:
            sn = add("ShaderNodeEnsoSmoothNormals", -1340, -160)
            if style == 'HAIR':
                sn.inputs["Tube"].default_value = 1.0
                sn.inputs["Roundness"].default_value = 0.9
            links.new(sn.outputs["Normal"], ev.inputs["Normal"])
            links.new(lit, cel.inputs["Value"])
            if style == 'CLEAN':
                cel.inputs["Bands"].default_value = 2.0
            diffuse = mul_color(info.outputs["Color"], cel.outputs["Shade"], -150, 60, "Light Color")
            if style == 'HAIR':
                # The ring: hair flows down from the top of the head; the node splits it into
                # locks so the ring zigzags. Plug a texture in Strands to draw your own.
                ring = add("ShaderNodeEnsoHairRing", -940, -380)
                ring.inputs["Position"].default_value = 0.65
                links.new(ev.outputs["Direction"], ring.inputs["Light Direction"])
                # Only where the light reaches.
                on = math('MULTIPLY', -700, -380, ring.outputs["Ring"], cel.outputs["Shade"], "Ring In Light")
                glossy = mul_color(info.outputs["Color"], on, -150, -300, "Ring Color")
                links.new(glossy, acc.inputs["Glossy Light"])
    elif style == 'HALFTONE':
        tc = add("ShaderNodeTexCoord", -1100, -320)
        vor = add("ShaderNodeTexVoronoi", -880, -320, "Dots")
        vor.feature = 'DISTANCE_TO_EDGE'
        vor.inputs["Scale"].default_value = 60.0
        vor.inputs["Randomness"].default_value = 0.0
        links.new(tc.outputs["Window"], vor.inputs["Vector"])
        # Dots of shadow grow from the cell centres as the light fades.
        mr = add("ShaderNodeMapRange", -760, -200, "Light Range")
        mr.clamp = True
        mr.inputs["From Min"].default_value = 0.05
        mr.inputs["From Max"].default_value = 0.6
        links.new(lit, mr.inputs["Value"])
        shade = math('SUBTRACT', -620, -60, 1.0, mr.outputs["Result"], "Shadow Amount")
        dots = math('LESS_THAN', -400, -200, vor.outputs["Distance"],
                    math('MULTIPLY', -560, -260, math('SUBTRACT', -700, -300, 1.0, shade), 0.55),
                    "Halftone")
        diffuse = mul_color(info.outputs["Color"], dots, -150, 60, "Light Color")
    else:
        bands = ramp(lit, -620, -60, ((0.0, (0, 0, 0)), (0.2, (0.35, 0.35, 0.35)), (0.45, (1, 1, 1))),
                     'CONSTANT', "Cel Bands")
        diffuse = mul_color(info.outputs["Color"], bands, -150, 60, "Light Color")
        if style == 'CEL_SPEC':
            gev = add("ShaderNodeLightEvaluation", -1100, -380, "Highlight")
            gev.mode = 'GLOSSY'
            gev.inputs["Roughness"].default_value = 0.2
            spec = math('MULTIPLY', -880, -380, gev.outputs["Factor"], info.outputs["Power"])
            hl = ramp(spec, -620, -380, ((0.0, (0, 0, 0)), (0.2, (1, 1, 1))), 'CONSTANT', "Highlight Size")
            glossy = mul_color(info.outputs["Color"], hl, -150, -300, "Highlight Color")
            links.new(glossy, acc.inputs["Glossy Light"])
        elif style == 'CEL_RIM':
            lw = add("ShaderNodeLayerWeight", -1100, -380)
            lw.inputs["Blend"].default_value = 0.35
            rim_edge = ramp(lw.outputs["Facing"], -880, -380, ((0.0, (0, 0, 0)), (0.65, (1, 1, 1))),
                            'CONSTANT', "Rim Width")
            # Only on the side the light reaches, so each light draws its own rim.
            rim = mul_color(rim_edge, bands, -620, -300, "Rim On Lit Side")
            rim = mul_color(rim, info.outputs["Color"], -400, -300, "Rim Color")
            diffuse = add_color(diffuse, rim, 80, -140, "Add Rim")
    # Per-light Ensō settings (Light properties > Ensō NPR Light). A light without them reads
    # zero everywhere, which leaves the light as it is.
    def light_setting(name, dx, dy):
        a = add("ShaderNodeAttribute", dx, dy, name)
        a.attribute_type = 'LIGHT'
        a.attribute_name = name
        a.hide = True
        return a

    if style in {'CLEAN', 'HAIR'}:
        soft = light_setting("enso_soft", -820, 330)
        shift = light_setting("enso_shift", -820, 290)
        links.new(soft.outputs["Fac"], cel.inputs["Softness"])
        links.new(math('ADD', -700, 290, cel.inputs["Threshold"].default_value, shift.outputs["Fac"],
                       "Band Shift"), cel.inputs["Threshold"])
    # Coloured shadows: the part of the surface this light does not reach gets its shadow colour.
    def gray_of(col, dx, dy):
        bw = add("ShaderNodeRGBToBW", dx, dy)
        bw.hide = True
        links.new(col, bw.inputs[0])
        return bw.outputs[0]

    reach = math('DIVIDE', 80, 330, gray_of(diffuse, -60, 330),
                 math('MAXIMUM', -60, 290, gray_of(info.outputs["Color"], -200, 290), 0.0001), "Reach")
    reach.node.use_clamp = True
    in_shadow = math('SUBTRACT', 80, 290, 1.0, reach, "In Shadow")
    amount = light_setting("enso_shadow_amount", -60, 250)
    shadow_col = light_setting("enso_shadow_color", -60, 210)
    tint = add("ShaderNodeMix", 220, 300, "Shadow Colour")
    tint.data_type = 'RGBA'
    tint.blend_type = 'ADD'
    links.new(math('MULTIPLY', 80, 250, in_shadow, amount.outputs["Fac"]), tint.inputs[0])
    links.new(diffuse, tint.inputs[6])
    links.new(shadow_col.outputs["Color"], tint.inputs[7])
    # Flat fill: the light's plain colour everywhere.
    flat = light_setting("enso_flat", 220, 120)
    fill = add("ShaderNodeMix", 380, 220, "Flat Fill")
    fill.data_type = 'RGBA'
    fill.clamp_factor = True
    links.new(flat.outputs["Fac"], fill.inputs[0])
    links.new(tint.outputs[2], fill.inputs[6])
    links.new(info.outputs["Color"], fill.inputs[7])
    diffuse = fill.outputs[2]
    acc.location = (x + 560, y)

    links.new(diffuse, acc.inputs["Diffuse Light"])
    frame.select = True
    tree.nodes.active = acc
    return acc


classes = (
    NODE_OT_enso_add_for_each_light,
    PREFERENCES_OT_enso_dlc_install,
    PREFERENCES_OT_enso_dlc_remove,
    PREFERENCES_OT_enso_license_activate,
    PREFERENCES_OT_enso_license_deactivate,
    PREFERENCES_OT_enso_license_page,
    NODE_OT_enso_unlock,
    USERPREF_PT_enso_dlc,
)


def _append_context_menu():
    # bl_ui registers after bl_operators, so hook the node context menu once everything is up.
    menu = getattr(bpy.types, "NODE_MT_context_menu", None)
    if menu is None:
        return 0.5
    menu.append(draw_node_context_menu)
    return None


# ---- Ensō Ink Bleed: edge data -------------------------------------------------------------
# The Ink Bleed node paints drips measured on the object. Its data comes from a small geometry
# nodes modifier; objects that use the node in one of their materials get it automatically.
# Ensō Curvature reads the screen since 1.1 and no longer needs the modifier.
_INK = {"ShaderNodeEnsoInkBleed"}
_ink_mats = {}


def _tree_has_ink(tree, seen=None):
    if tree is None:
        return False
    seen = seen or set()
    if tree.name in seen:
        return False
    seen.add(tree.name)
    for n in tree.nodes:
        if n.bl_idname in _INK:
            return True
        if n.bl_idname == "ShaderNodeGroup" and _tree_has_ink(n.node_tree, seen):
            return True
    return False


@persistent
def _enso_edge_data_handler(scene, depsgraph=None):
    try:
        if bpy.app.is_job_running('RENDER'):
            return
    except Exception:
        pass
    try:
        changed = depsgraph is None or any(
            isinstance(u.id, (bpy.types.Material, bpy.types.NodeTree, bpy.types.Object))
            for u in depsgraph.updates)
        if not changed:
            return
        for u in (depsgraph.updates if depsgraph is not None else ()):
            if isinstance(u.id, (bpy.types.Material, bpy.types.NodeTree)):
                _ink_mats.clear()
                break
        import enso_edge_data
        for ob in scene.objects:
            if ob.type != 'MESH':
                continue
            for slot in ob.material_slots:
                m = slot.material
                if m is None or not m.use_nodes:
                    continue
                key = m.name
                if key not in _ink_mats:
                    _ink_mats[key] = _tree_has_ink(m.node_tree)
                if _ink_mats[key]:
                    enso_edge_data.ensure_modifier(ob)
                    break
    except Exception as ex:
        print("Ensō edge data:", ex)


def _add_pack_addon_paths():
    """Put the pack add-on folders on the module path before Blender enables the add-ons saved in
    the preferences, so they load at startup without "Add-on not loaded" warnings."""
    import sys
    try:
        for pack_dir, _manifest in active_packs():
            addons_dir = os.path.join(pack_dir, "addons")
            if os.path.isdir(addons_dir):
                _add_pack_path(addons_dir)
    except Exception as ex:
        print("Ensō DLC: could not read the installed packs", ex)


def register():
    bpy.types.WindowManager.enso_license_key = StringProperty(
        name="License Key", description="Your Ensō NPR Pack license key", options={'SKIP_SAVE'})
    _add_pack_addon_paths()
    bpy.app.timers.register(_renew_licenses, first_interval=4.0, persistent=True)
    bpy.app.handlers.load_post.append(_enso_dlc_load_post)
    bpy.app.handlers.depsgraph_update_post.append(_enso_lights_handler)
    bpy.app.handlers.depsgraph_update_post.append(_enso_edge_data_handler)
    bpy.app.handlers.frame_change_post.append(_enso_lights_handler)
    bpy.app.handlers.render_pre.append(_enso_lights_handler)
    bpy.app.timers.register(_append_context_menu, first_interval=0.0, persistent=True)


def unregister():
    if bpy.app.timers.is_registered(_renew_licenses):
        bpy.app.timers.unregister(_renew_licenses)
    try:
        del bpy.types.WindowManager.enso_license_key
    except Exception:
        pass
    if _enso_dlc_load_post in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(_enso_dlc_load_post)
    for h in (bpy.app.handlers.depsgraph_update_post, bpy.app.handlers.frame_change_post,
              bpy.app.handlers.render_pre):
        if _enso_lights_handler in h:
            h.remove(_enso_lights_handler)
    if _enso_edge_data_handler in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_enso_edge_data_handler)
    menu = getattr(bpy.types, "NODE_MT_context_menu", None)
    if menu is not None:
        menu.remove(draw_node_context_menu)
