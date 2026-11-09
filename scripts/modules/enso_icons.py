# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Ensō brush-ink icons for Ensō's own menus (the PNGs live in enso_icon_images/).

    import enso_icons
    layout.operator(..., icon_value=enso_icons.get("leak"))
"""

import os

_previews = None


def _load():
    global _previews
    import bpy.utils.previews
    _previews = bpy.utils.previews.new()
    d = os.path.join(os.path.dirname(__file__), "enso_icon_images")
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            if f.endswith(".png"):
                _previews.load(f[:-4], os.path.join(d, f), 'IMAGE')


def get(name):
    """icon_value of an Ensō icon, 0 when it is missing."""
    if _previews is None:
        try:
            _load()
        except Exception:
            return 0
    item = _previews.get(name)
    return item.icon_id if item else 0
