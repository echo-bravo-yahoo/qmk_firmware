"""
Route-debug dump: print the generator's view of a system — bodies, Lagrange
points, leg types, framing — to eyeball placement. Delegates to the firmware's
route_gen_describe() (host build).

    python3 route_dump.py LV-426 [KG-348 ...]

Arguments are designation tokens of the form {LV,BG,KG,RF}-{digits}: the token IS
the destination (echoed verbatim in the describe() header) and its prefix pins the
[DEST] body's type. A malformed token falls back to a class-derived designation.
The input token is echoed first for traceability.
"""
import sys
import oled_gfx_lib as g

if __name__ == "__main__":
    for desig in (sys.argv[1:] or ["LV-426"]):
        print(f"### designation token: {desig}")
        g.describe(desig)
