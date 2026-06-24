"""
ctypes bridge to the firmware graphics code.

Compiles oled_gfx.c + route_gen.c + starmap_world.c into oled_gfx.so (with
-DRG_HOST so route_gen_describe is available) and exposes the route pipeline to
Python. Auto-rebuilds when any source is newer than the .so, so the preview and
tests always exercise the real firmware code — the single source of truth.
"""
import ctypes, os, subprocess
from PIL import Image

_DIR    = os.path.dirname(os.path.abspath(__file__))
_KM     = os.path.dirname(_DIR)                 # keymaps/aeby
# Firmware sources (the single source of truth) + the host-only QMK shim that
# stands in for the timer / OLED / eeconfig symbols route_anim.c references.
_FW_SRCS  = [os.path.join(_KM, f) for f in
             ("oled_gfx.c", "route_gen.c", "starmap_world.c", "route_anim.c")]
_HOST_SRCS = [os.path.join(_DIR, "host_qmk_shim.c")]
_SRCS   = _FW_SRCS + _HOST_SRCS
_HDRS   = ([os.path.join(_KM, f) for f in
            ("oled_gfx.h", "route_gen.h", "starmap_world.h", "route_anim.h")] +
           [os.path.join(_DIR, "oled_gfx_stub.h")])
_LIB_SO = os.path.join(_DIR, "oled_gfx.so")

W, H = 128, 32
ON   = (0, 210, 0)

GFX_MARKER_RING   = 0
GFX_MARKER_CROSS  = 1
GFX_MARKER_BODY   = 2
GFX_MARKER_TARGET = 3   # destination reticle

GFX_LEG_TRANSFER = 0
GFX_LEG_COAST    = 1

# ── Structs (field order must mirror oled_gfx.h exactly) ──────────────────────

class _GfxArc(ctypes.Structure):
    _fields_ = [('radius', ctypes.c_uint8), ('solid', ctypes.c_bool),
                ('gap_px', ctypes.c_uint8), ('local_center', ctypes.c_bool),
                ('cx', ctypes.c_int16), ('cy', ctypes.c_int16)]

class _GfxLeg(ctypes.Structure):
    _fields_ = [('type', ctypes.c_uint8),
                ('p0x', ctypes.c_int16), ('p0y', ctypes.c_int16),
                ('p1x', ctypes.c_int16), ('p1y', ctypes.c_int16),
                ('cx',  ctypes.c_int16), ('cy',  ctypes.c_int16),
                ('arc_r', ctypes.c_uint8),
                ('a0', ctypes.c_float), ('a_sweep', ctypes.c_float),
                ('len', ctypes.c_float)]

class _GfxMarker(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int16), ('y', ctypes.c_int16),
                ('type', ctypes.c_int)]

class _GfxPt(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int16), ('y', ctypes.c_int16)]

class _StarmapBody(ctypes.Structure):
    _fields_ = [('type', ctypes.c_uint8), ('parent_idx', ctypes.c_uint8),
                ('orbital_radius', ctypes.c_float), ('angle', ctypes.c_float)]

class _StarmapSystem(ctypes.Structure):
    _fields_ = [
        ('bodies',       _StarmapBody * 16),
        ('body_count',   ctypes.c_uint8),
        ('planet_idx',   ctypes.c_uint8 * 6),
        ('planet_count', ctypes.c_uint8),
        ('depart_idx',   ctypes.c_uint8),
        ('dest_idx',     ctypes.c_uint8),
        ('name',         ctypes.c_char * 12),
        ('designation',  ctypes.c_char * 8),
        ('seed',         ctypes.c_uint32),
    ]

# Lagrange selectors (match starmap_lagrange_t).
L1, L2, L3, L4, L5 = 0, 1, 2, 3, 4
STARMAP_PLANET, STARMAP_MOON, STARMAP_TROJAN, STARMAP_VAGRANT = 0, 1, 2, 3


class _GfxRoute(ctypes.Structure):
    _fields_ = [
        ('arc_cx',         ctypes.c_int16),
        ('arc_cy',         ctypes.c_int16),
        ('arc_count',      ctypes.c_uint8),
        ('arcs',           _GfxArc * 8),
        ('dash_px',        ctypes.c_uint8),
        ('gap_px',         ctypes.c_uint8),
        ('leg_count',      ctypes.c_uint8),
        ('legs',           _GfxLeg * 4),
        ('marker_count',   ctypes.c_uint8),
        ('markers',        _GfxMarker * 4),
        ('body_count',     ctypes.c_uint8),
        ('bodies',         _GfxPt * 6),
        ('ship_offset_x',  ctypes.c_int8),
        ('eta_minutes',    ctypes.c_uint16),
        ('system_name',    ctypes.c_char * 12),
        ('designation',    ctypes.c_char * 8),
    ]

# Journey state machine (route_anim.h). Mirrors route_journey_t / route_telemetry_t.

class _RouteJourney(ctypes.Structure):
    _fields_ = [
        ('route',       _GfxRoute),
        ('bg_cache',    ctypes.c_uint8 * 512),
        ('start_ms',    ctypes.c_uint32),
        ('duration_ms', ctypes.c_uint32),
        ('dest_rng',    ctypes.c_uint32),
        ('cur_t',       ctypes.c_float),
        ('active',      ctypes.c_bool),
    ]

class _RouteTelemetry(ctypes.Structure):
    _fields_ = [
        ('system_name',       ctypes.c_char * 8),
        ('designation',       ctypes.c_char * 8),
        ('eta_remaining_min', ctypes.c_uint16),
        ('phase',             ctypes.c_uint8),
        ('burn',              ctypes.c_uint8),
        ('gaming',            ctypes.c_uint8),
    ]

# Journey phases (match gfx_phase_t in oled_gfx.h).
GFX_PHASE_DEPART, GFX_PHASE_TRANSIT, GFX_PHASE_FLYBY, GFX_PHASE_COAST, GFX_PHASE_ARRIVE = 0, 1, 2, 3, 4
PHASE_NAMES = {
    GFX_PHASE_DEPART:  "DEPART",
    GFX_PHASE_TRANSIT: "TRANSIT",
    GFX_PHASE_FLYBY:   "FLYBY",
    GFX_PHASE_COAST:   "COAST",
    GFX_PHASE_ARRIVE:  "ARRIVE",
}

# ── Build / load ──────────────────────────────────────────────────────────────

def _build():
    subprocess.run([
        "gcc", "-shared", "-fPIC", "-O2",
        f"-I{_DIR}",
        "-DRG_HOST",
        # No shell here: the value's double quotes must reach the preprocessor
        # so #include QMK_KEYBOARD_H expands to #include "oled_gfx_stub.h".
        '-DQMK_KEYBOARD_H="oled_gfx_stub.h"',
        *_SRCS, "-o", _LIB_SO, "-lm",
    ], check=True)


def _lib():
    src_mtime = max(os.path.getmtime(p) for p in _SRCS + _HDRS)
    if not os.path.exists(_LIB_SO) or os.path.getmtime(_LIB_SO) < src_mtime:
        _build()
    return ctypes.CDLL(_LIB_SO)


_LIB = None

def _get_lib():
    global _LIB
    if _LIB is None:
        lib = _lib()
        lib.route_gen_build.argtypes      = [ctypes.c_char_p, ctypes.POINTER(_GfxRoute)]
        lib.route_gen_build.restype       = None
        lib.route_gen_describe.argtypes    = [ctypes.c_char_p]
        lib.route_gen_describe.restype     = None
        lib.gfx_route_bake_bg.argtypes     = [ctypes.POINTER(_GfxRoute), ctypes.c_char_p]
        lib.gfx_route_bake_bg.restype      = None
        lib.gfx_route_bake_ship.argtypes   = [ctypes.POINTER(_GfxRoute), ctypes.c_float,
                                              ctypes.c_uint8, ctypes.c_char_p]
        lib.gfx_route_bake_ship.restype    = None
        lib.gfx_route_ship_pos.argtypes    = [ctypes.POINTER(_GfxRoute), ctypes.c_float,
                                              ctypes.POINTER(ctypes.c_int16),
                                              ctypes.POINTER(ctypes.c_int16)]
        lib.gfx_route_ship_pos.restype     = None
        lib.gfx_pulse_size.argtypes        = [ctypes.c_uint32]
        lib.gfx_pulse_size.restype         = ctypes.c_uint8
        lib.gfx_route_phase.argtypes       = [ctypes.POINTER(_GfxRoute), ctypes.c_float,
                                              ctypes.POINTER(ctypes.c_bool)]
        lib.gfx_route_phase.restype        = ctypes.c_int
        lib.gfx_burn_warning.argtypes      = [ctypes.c_char_p, ctypes.c_uint32]
        lib.gfx_burn_warning.restype       = None
        lib.gfx_route_sizeof.argtypes      = []
        lib.gfx_route_sizeof.restype       = ctypes.c_uint32
        lib.starmap_seed.argtypes          = [ctypes.c_char_p]
        lib.starmap_seed.restype           = ctypes.c_uint32
        lib.starmap_build.argtypes         = [ctypes.c_char_p, ctypes.POINTER(_StarmapSystem)]
        lib.starmap_build.restype          = None
        lib.starmap_world_pos.argtypes     = [ctypes.POINTER(_StarmapSystem), ctypes.c_int,
                                              ctypes.POINTER(ctypes.c_float),
                                              ctypes.POINTER(ctypes.c_float)]
        lib.starmap_world_pos.restype      = None
        lib.starmap_lagrange_pos.argtypes  = [ctypes.POINTER(_StarmapSystem), ctypes.c_int,
                                              ctypes.c_int,
                                              ctypes.POINTER(ctypes.c_float),
                                              ctypes.POINTER(ctypes.c_float)]
        lib.starmap_lagrange_pos.restype   = None
        # Journey state machine + host clock/eeconfig controls.
        lib.route_anim_init.argtypes          = [ctypes.POINTER(_RouteJourney)]
        lib.route_anim_init.restype           = None
        lib.route_anim_render_map.argtypes    = [ctypes.POINTER(_RouteJourney)]
        lib.route_anim_render_map.restype     = None
        lib.route_anim_fill_telemetry.argtypes = [ctypes.POINTER(_RouteJourney),
                                                  ctypes.POINTER(_RouteTelemetry)]
        lib.route_anim_fill_telemetry.restype = None
        lib.route_anim_reroll.argtypes        = [ctypes.POINTER(_RouteJourney)]
        lib.route_anim_reroll.restype         = None
        lib.route_journey_sizeof.argtypes     = []
        lib.route_journey_sizeof.restype      = ctypes.c_uint32
        lib.route_telemetry_sizeof.argtypes   = []
        lib.route_telemetry_sizeof.restype    = ctypes.c_uint32
        for fn in ("host_set_now_ms", "host_set_boot_counter"):
            getattr(lib, fn).argtypes = [ctypes.c_uint32]
            getattr(lib, fn).restype  = None
        for fn in ("host_get_now_ms", "host_get_boot_counter"):
            getattr(lib, fn).argtypes = []
            getattr(lib, fn).restype  = ctypes.c_uint32
        # Verify the ctypes layout matches the C structs (catches padding drift).
        for name, py, c in (("gfx_route_t", _GfxRoute, lib.gfx_route_sizeof()),
                            ("route_journey_t", _RouteJourney, lib.route_journey_sizeof()),
                            ("route_telemetry_t", _RouteTelemetry, lib.route_telemetry_sizeof())):
            assert ctypes.sizeof(py) == c, f"{name} size mismatch: python {ctypes.sizeof(py)} != C {c}"
        _LIB = lib
    return _LIB

# ── Struct ↔ dict ─────────────────────────────────────────────────────────────

def _struct_to_dict(s: _GfxRoute) -> dict:
    return {
        'arc_cx': s.arc_cx, 'arc_cy': s.arc_cy,
        'arcs': [{'radius': s.arcs[i].radius, 'solid': bool(s.arcs[i].solid),
                  'gap_px': s.arcs[i].gap_px, 'local_center': bool(s.arcs[i].local_center),
                  'cx': s.arcs[i].cx, 'cy': s.arcs[i].cy}
                 for i in range(s.arc_count)],
        'dash_px': s.dash_px, 'gap_px': s.gap_px,
        'legs': [{'type': s.legs[i].type,
                  'p0x': s.legs[i].p0x, 'p0y': s.legs[i].p0y,
                  'p1x': s.legs[i].p1x, 'p1y': s.legs[i].p1y,
                  'cx': s.legs[i].cx, 'cy': s.legs[i].cy,
                  'arc_r': s.legs[i].arc_r, 'a0': s.legs[i].a0,
                  'a_sweep': s.legs[i].a_sweep, 'len': s.legs[i].len}
                 for i in range(s.leg_count)],
        'markers': [{'x': s.markers[i].x, 'y': s.markers[i].y, 'type': s.markers[i].type}
                    for i in range(s.marker_count)],
        'bodies': [{'x': s.bodies[i].x, 'y': s.bodies[i].y} for i in range(s.body_count)],
        'ship_offset_x': s.ship_offset_x,
        'eta_minutes': s.eta_minutes,
        'system_name': s.system_name.decode('ascii', 'replace'),
        'designation': s.designation.decode('ascii', 'replace'),
    }


def _to_struct(d: dict) -> _GfxRoute:
    s = _GfxRoute()
    s.arc_cx, s.arc_cy = d['arc_cx'], d['arc_cy']
    s.arc_count = len(d['arcs'])
    for i, a in enumerate(d['arcs']):
        s.arcs[i].radius       = a['radius']
        s.arcs[i].solid        = a['solid']
        s.arcs[i].gap_px       = a.get('gap_px', 0)
        s.arcs[i].local_center = a.get('local_center', False)
        s.arcs[i].cx           = a.get('cx', 0)
        s.arcs[i].cy           = a.get('cy', 0)
    s.dash_px, s.gap_px = d['dash_px'], d['gap_px']
    s.leg_count = len(d['legs'])
    for i, l in enumerate(d['legs']):
        s.legs[i].type    = l.get('type', GFX_LEG_TRANSFER)
        s.legs[i].p0x, s.legs[i].p0y = l['p0x'], l['p0y']
        s.legs[i].p1x, s.legs[i].p1y = l['p1x'], l['p1y']
        s.legs[i].cx,  s.legs[i].cy  = l['cx'], l['cy']
        s.legs[i].arc_r   = l.get('arc_r', 0)
        s.legs[i].a0      = l.get('a0', 0.0)
        s.legs[i].a_sweep = l.get('a_sweep', 0.0)
        s.legs[i].len     = l.get('len', 0.0)
    s.marker_count = len(d['markers'])
    for i, m in enumerate(d['markers']):
        s.markers[i].x, s.markers[i].y, s.markers[i].type = m['x'], m['y'], m['type']
    s.body_count = len(d['bodies'])
    for i, b in enumerate(d['bodies']):
        s.bodies[i].x, s.bodies[i].y = b['x'], b['y']
    s.ship_offset_x = d['ship_offset_x']
    s.eta_minutes   = d.get('eta_minutes', 0)
    s.system_name   = d.get('system_name', '').encode('ascii', 'replace')[:11]
    s.designation   = d.get('designation', '').encode('ascii', 'replace')[:7]
    return s

# ── Public API ────────────────────────────────────────────────────────────────

def generate_route(designation: str) -> dict:
    """Generate a gfx_route_t dict from an LV-NNN designation via route_gen_build."""
    s = _GfxRoute()
    _get_lib().route_gen_build(designation.encode(), ctypes.byref(s))
    return _struct_to_dict(s)


def describe(designation: str) -> None:
    """Print the route_gen_describe() dump (bodies, Lagrange points, legs) to stdout."""
    _get_lib().route_gen_describe(designation.encode())


def seed(designation: str) -> int:
    return _get_lib().starmap_seed(designation.encode())


def build_system(designation: str) -> _StarmapSystem:
    """The raw starmap_system_t for a designation (bodies, planet indices, names)."""
    sys = _StarmapSystem()
    _get_lib().starmap_build(designation.encode(), ctypes.byref(sys))
    return sys


def world_pos(sys: _StarmapSystem, idx: int) -> tuple[float, float]:
    x, y = ctypes.c_float(), ctypes.c_float()
    _get_lib().starmap_world_pos(ctypes.byref(sys), idx, ctypes.byref(x), ctypes.byref(y))
    return x.value, y.value


def lagrange_pos(sys: _StarmapSystem, planet_body_idx: int, which: int) -> tuple[float, float]:
    x, y = ctypes.c_float(), ctypes.c_float()
    _get_lib().starmap_lagrange_pos(ctypes.byref(sys), planet_body_idx, which,
                                    ctypes.byref(x), ctypes.byref(y))
    return x.value, y.value


def bake_bg(route: dict) -> bytearray:
    """512-byte SSD1306 page buffer for the static background."""
    s = _to_struct(route)
    buf = ctypes.create_string_buffer(512)
    _get_lib().gfx_route_bake_bg(ctypes.byref(s), buf)
    return bytearray(buf.raw)


def bake_ship(route: dict, t: float, pulse: int, buf: bytearray) -> None:
    """OR the ship crosshair (at journey t, given pulse size) into buf in place."""
    s = _to_struct(route)
    cbuf = ctypes.create_string_buffer(bytes(buf), 512)
    _get_lib().gfx_route_bake_ship(ctypes.byref(s), ctypes.c_float(t),
                                   ctypes.c_uint8(pulse), cbuf)
    buf[:] = bytearray(cbuf.raw)


def ship_pos(route: dict, t: float) -> tuple[int, int]:
    """(x, y) of the ship crosshair at journey t∈[0,1] — matches the firmware."""
    s = _to_struct(route)
    ox, oy = ctypes.c_int16(), ctypes.c_int16()
    _get_lib().gfx_route_ship_pos(ctypes.byref(s), ctypes.c_float(t),
                                  ctypes.byref(ox), ctypes.byref(oy))
    return ox.value, oy.value


def pulse_size(now_ms: int) -> int:
    return _get_lib().gfx_pulse_size(ctypes.c_uint32(now_ms))


def route_phase(route: dict, t: float) -> tuple[int, bool]:
    """(phase, burn) at journey t∈[0,1] — exactly what the firmware derives."""
    s = _to_struct(route)
    burn = ctypes.c_bool()
    phase = _get_lib().gfx_route_phase(ctypes.byref(s), ctypes.c_float(t), ctypes.byref(burn))
    return phase, bool(burn.value)


def burn_warning(now_ms: int = 0) -> bytearray:
    """512-byte slave-portrait page buffer for the big BURN takeover at now_ms."""
    buf = ctypes.create_string_buffer(512)
    _get_lib().gfx_burn_warning(buf, ctypes.c_uint32(now_ms))
    return bytearray(buf.raw)


def buf_to_image(buf: bytearray) -> Image.Image:
    """512-byte SSD1306 page buffer → 128×32 PIL image."""
    img = Image.new("RGB", (W, H), (0, 0, 0))
    for page in range(4):
        for x in range(W):
            byte = buf[page * W + x]
            for bit in range(8):
                if byte & (1 << bit):
                    img.putpixel((x, page * 8 + bit), ON)
    return img


def buf_to_image_portrait(buf: bytearray) -> Image.Image:
    """Slave-portrait page buffer (32 wide × 128 tall, stride 32) → 32×128 image.

    This is the slave's OLED_ROTATION_270 logical surface — what gfx_burn_warning
    writes and oled_write_raw blits — so the image matches the live panel pixel
    for pixel (the warning's landscape word runs down the long 128 axis)."""
    img = Image.new("RGB", (32, 128), (0, 0, 0))
    for page in range(16):
        for x in range(32):
            byte = buf[page * 32 + x]
            for bit in range(8):
                if byte & (1 << bit):
                    img.putpixel((x, page * 8 + bit), ON)
    return img


def render_route_image(route: dict) -> Image.Image:
    """Render the static background (no ship) to a 128×32 image."""
    return buf_to_image(bake_bg(route))


# ── Journey state machine (route_anim.c, driven by the simulated clock) ─────────

def set_now_ms(ms: int) -> None:
    """Set the simulated millisecond clock (timer_read32/timer_elapsed32)."""
    _get_lib().host_set_now_ms(int(ms) & 0xFFFFFFFF)


def set_boot_counter(n: int) -> None:
    """Set the persisted eeconfig boot counter that seeds the opening route."""
    _get_lib().host_set_boot_counter(int(n) & 0xFFFFFFFF)


def anim_init():
    """route_anim_init(): pick the opening route from the boot counter + clock."""
    j = _RouteJourney()
    _get_lib().route_anim_init(ctypes.byref(j))
    return j


def anim_render_map(j) -> None:
    """route_anim_render_map(): advance the clock-driven progress; regen on arrival."""
    _get_lib().route_anim_render_map(ctypes.byref(j))


def anim_reroll(j) -> None:
    """route_anim_reroll(): jump to the next system immediately (the re-roll key)."""
    _get_lib().route_anim_reroll(ctypes.byref(j))


def anim_fill_telemetry(j) -> dict:
    """route_anim_fill_telemetry(): the synced master→slave telemetry snapshot."""
    t = _RouteTelemetry()
    _get_lib().route_anim_fill_telemetry(ctypes.byref(j), ctypes.byref(t))
    return {
        'system_name': t.system_name.decode('ascii', 'replace'),
        'designation': t.designation.decode('ascii', 'replace'),
        'eta_remaining_min': t.eta_remaining_min,
        'phase': t.phase,
        'burn': bool(t.burn),
        'gaming': t.gaming,
    }


def journey_route(j) -> dict:
    """The active route inside a journey, as a gfx_route_t dict."""
    return _struct_to_dict(j.route)
