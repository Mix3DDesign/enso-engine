# SPDX-FileCopyrightText: 2026 Mix 3D Design
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""
Ensō Engine license check for paid DLC packs.

A pack that needs a license is unlocked by a token signed by the Mix 3D Design license
server (Ed25519). The token names the license key (the buyer ID), this computer and an
expiry date, so it keeps working offline for a while. Ensō renews it in the background once
a day while it can reach the server; a key that was revoked or blacklisted (a Patreon member
who cancelled, a refund, a leaked key) gets no new token, and the pack switches off.

Nothing is ever deleted: a pack without a valid license just isn't loaded.

Privacy: the server only sees the license key and a one-way hash of this computer.
"""

import hashlib
import json
import os
import platform
import threading
import time
import uuid

SERVER = os.environ.get("ENSO_LICENSE_SERVER", "https://enso-license.akhbarmix34.workers.dev")
# Public keys of the license server. Several are allowed so the key can be rotated.
PUBLIC_KEYS = (
    "470248ef2d3fec7275d3633e201d9ba4c6db37caca06a9cf92845dc175833245",
)
# Packs that need a license, whatever their manifest says. These are the Patreon editions;
# the shop editions (bought once) have no license check at all.
LICENSED_PACKS = {"enso_npr_pack_1_member"}


def product_of(pack_id):
    """The product a pack edition belongs to (the license server issues keys per product)."""
    return pack_id[:-len("_member")] if pack_id.endswith("_member") else pack_id

RENEW_AFTER = 24 * 3600         # renew once a day when online
CLOCK_SLACK = 2 * 24 * 3600     # how far the clock may go backwards before a token is distrusted

# ---------------------------------------------------------------------------------- Ed25519
# Verification only (RFC 8032), pure Python. Slow-ish (a few ms) but done rarely.

_p = 2 ** 255 - 19
_L = 2 ** 252 + 27742317777372353535851937790883648493
_d = (-121665 * pow(121666, _p - 2, _p)) % _p
_I = pow(2, (_p - 1) // 4, _p)


def _recover_x(y, sign):
    if y >= _p:
        return None
    x2 = (y * y - 1) * pow(_d * y * y + 1, _p - 2, _p)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_p + 3) // 8, _p)
    if (x * x - x2) % _p != 0:
        x = x * _I % _p
    if (x * x - x2) % _p != 0:
        return None
    if (x & 1) != sign:
        x = _p - x
    return x


_Gy = 4 * pow(5, _p - 2, _p) % _p
_Gx = _recover_x(_Gy, 0)
_G = (_Gx, _Gy, 1, _Gx * _Gy % _p)


def _add(P, Q):
    A = (P[1] - P[0]) * (Q[1] - Q[0]) % _p
    B = (P[1] + P[0]) * (Q[1] + Q[0]) % _p
    C = 2 * P[3] * Q[3] * _d % _p
    D = 2 * P[2] * Q[2] % _p
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % _p, G * H % _p, F * G % _p, E * H % _p)


def _mul(s, P):
    Q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            Q = _add(Q, P)
        P = _add(P, P)
        s >>= 1
    return Q


def _equal(P, Q):
    return ((P[0] * Q[2] - Q[0] * P[2]) % _p == 0 and
            (P[1] * Q[2] - Q[1] * P[2]) % _p == 0)


def _decompress(b):
    if len(b) != 32:
        return None
    y = int.from_bytes(b, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % _p)


def ed25519_verify(public, msg, sig):
    if len(public) != 32 or len(sig) != 64:
        return False
    A = _decompress(public)
    R = _decompress(sig[:32])
    if A is None or R is None:
        return False
    s = int.from_bytes(sig[32:], "little")
    if s >= _L:
        return False
    h = int.from_bytes(hashlib.sha512(sig[:32] + public + msg).digest(), "little") % _L
    return _equal(_mul(s, _G), _add(R, _mul(h, A)))


# ---------------------------------------------------------------------------------- tokens

def _b64u_dec(s):
    import base64
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def parse_token(token):
    """Payload dict of a correctly signed token, else None."""
    try:
        a, b = token.split(".")
        body, sig = _b64u_dec(a), _b64u_dec(b)
    except Exception:
        return None
    for pk in PUBLIC_KEYS:
        try:
            if ed25519_verify(bytes.fromhex(pk), body, sig):
                return json.loads(body.decode("utf-8"))
        except Exception:
            continue
    return None


_machine = None


def machine_id():
    """A stable, anonymous id for this computer (a hash, nothing readable is sent)."""
    global _machine
    if _machine:
        return _machine
    parts = [platform.node(), platform.system(), platform.machine(), str(uuid.getnode())]
    if os.name == "nt":
        try:
            import winreg
            k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Cryptography", 0,
                               winreg.KEY_READ | getattr(winreg, "KEY_WOW64_64KEY", 0))
            parts.append(str(winreg.QueryValueEx(k, "MachineGuid")[0]))
        except Exception:
            pass
    else:
        for f in ("/etc/machine-id", "/var/lib/dbus/machine-id"):
            try:
                with open(f, encoding="utf-8") as fh:
                    parts.append(fh.read().strip())
                    break
            except Exception:
                pass
    _machine = hashlib.sha256(("enso|" + "|".join(parts)).encode("utf-8")).hexdigest()
    return _machine


def machine_label():
    return (platform.node() or "PC")[:30]


# ---------------------------------------------------------------------------------- storage

def _store_dir():
    import bpy
    return bpy.utils.user_resource('CONFIG', path="enso_licenses", create=True)


def _store_path(pack_id):
    safe = "".join(c for c in pack_id if c.isalnum() or c in "_-")
    return os.path.join(_store_dir(), safe + ".json")


def _load(pack_id):
    try:
        with open(_store_path(pack_id), encoding="utf-8") as fh:
            d = json.load(fh)
            return d if isinstance(d, dict) else {}
    except Exception:
        return {}


def _save(pack_id, data):
    path = _store_path(pack_id)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=1)
    os.replace(tmp, path)


# ---------------------------------------------------------------------------------- state

class State:
    __slots__ = ("ok", "message", "owner", "key", "expires", "kind")

    def __init__(self, ok, message, owner="", key="", expires=0, kind=""):
        self.ok, self.message, self.owner, self.key, self.expires, self.kind = ok, message, owner, key, expires, kind


def needs_license(manifest):
    return manifest.get("id") in LICENSED_PACKS or bool(manifest.get("license_required"))


def _mask(key):
    return key[:10] + "…" + key[-5:] if len(key) > 16 else key


def status(pack_id, manifest=None):
    """Whether a pack may load right now (offline check, no network)."""
    d = _load(pack_id)
    if d.get("blocked"):
        return State(False, d.get("message") or "This license is no longer active.", key=d.get("key", ""))
    tok = d.get("token")
    if not tok:
        return State(False, "Enter your license key to unlock this pack.")
    p = parse_token(tok)
    t = time.time()
    if p is None or p.get("p") != product_of(pack_id) or p.get("m") != machine_id():
        return State(False, "The saved license isn't valid on this computer. Activate it again.")
    stamped = (manifest or {}).get("buyer")
    if stamped and stamped != p.get("k"):
        return State(False, "This copy of the pack belongs to another license.")
    if t + CLOCK_SLACK < max(p.get("iat", 0), d.get("seen", 0)):
        return State(False, "Your computer's clock looks wrong. Fix the date and restart Ensō.")
    if t > p.get("exp", 0):
        return State(False, "The license needs to be renewed online. Connect to the internet and restart Ensō.",
                     key=p.get("k", ""))
    if t > d.get("seen", 0):
        d["seen"] = int(t)
        try:
            _save(pack_id, d)
        except Exception:
            pass
    return State(True, "Licensed", p.get("o", ""), p.get("k", ""), p.get("exp", 0), p.get("kind", ""))


def allowed(pack_id, manifest=None):
    return status(pack_id, manifest).ok


# ---------------------------------------------------------------------------------- network

def _post(path, body, timeout=12):
    import urllib.request
    import urllib.error
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(SERVER + path, data=data, method="POST",
                                 headers={"content-type": "application/json", "user-agent": "EnsoEngine"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8") or "{}")
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8") or "{}")
        except Exception:
            return e.code, {}
    except Exception as e:
        return 0, {"error": "offline", "message": "Couldn't reach the license server ({:s}).".format(type(e).__name__)}


def _accept(pack_id, res):
    tok = res.get("token", "")
    p = parse_token(tok)
    if p is None or p.get("p") != product_of(pack_id) or p.get("m") != machine_id():
        return False
    _save(pack_id, {"token": tok, "key": p.get("k"), "seen": int(time.time()), "renewed": int(time.time())})
    return True


def activate(pack_id, key):
    """Activate a license key on this computer. Returns (ok, message)."""
    code, res = _post("/v1/activate", {"key": key.strip(), "machine": machine_id(),
                                       "label": machine_label(), "pack": pack_id})
    if code == 200 and _accept(pack_id, res):
        return True, "Unlocked. Thank you, {:s}!".format(res.get("owner") or "and enjoy")
    return False, res.get("message") or "Activation failed (error {:d}).".format(code)


def renew(pack_id, force=False):
    """Renew the saved token. Returns (changed, message). A revoked key is marked blocked."""
    d = _load(pack_id)
    tok = d.get("token")
    if not tok or d.get("blocked"):
        return False, ""
    if not force and time.time() - d.get("renewed", 0) < RENEW_AFTER:
        return False, ""
    code, res = _post("/v1/refresh", {"token": tok, "machine": machine_id(), "label": machine_label()})
    if code == 200 and _accept(pack_id, res):
        return False, ""
    if code in (403,) and res.get("error") in ("revoked", "blacklisted", "machine_removed"):
        d["blocked"] = True
        d["message"] = res.get("message") or "This license is no longer active."
        d.pop("token", None)
        _save(pack_id, d)
        return True, d["message"]
    return False, res.get("message", "")


def deactivate(pack_id):
    """Free this computer's slot and forget the license here."""
    d = _load(pack_id)
    if d.get("token"):
        _post("/v1/deactivate", {"token": d["token"]}, timeout=8)
    try:
        os.remove(_store_path(pack_id))
    except Exception:
        pass


def forget_block(pack_id):
    """Let the user try a new key after a block."""
    d = _load(pack_id)
    if d.get("blocked"):
        try:
            os.remove(_store_path(pack_id))
        except Exception:
            pass


def renew_in_background(pack_ids, on_change):
    """Renew tokens without blocking the UI. on_change(pack_id, message) runs on the main thread
    for every pack that was switched on or off by the renewal."""
    import bpy
    if not getattr(bpy.app, "online_access", True):
        return
    results = []
    before = {pid: allowed(pid) for pid in pack_ids}

    def work():
        for pid in pack_ids:
            try:
                _changed, msg = renew(pid)
                if allowed(pid) != before[pid]:
                    results.append((pid, msg))
            except Exception as ex:
                print("Ensō license: renew failed:", ex)

    th = threading.Thread(target=work, daemon=True)
    th.start()

    def poll():
        if th.is_alive():
            return 1.0
        for pid, msg in results:
            try:
                on_change(pid, msg)
            except Exception as ex:
                print("Ensō license:", ex)
        return None

    bpy.app.timers.register(poll, first_interval=1.0, persistent=True)
