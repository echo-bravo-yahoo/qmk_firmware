"""
Route-debug dump: print the generator's view of a system — bodies, Lagrange
points, leg types, framing — to eyeball placement. Delegates to the firmware's
route_gen_describe() (host build).

    python3 route_dump.py LV-426 [LV-223 ...]

Arguments are seed tokens; the describe() header shows the class-derived
designation (LV/KG/BG/RF). The input token is echoed first for traceability.
"""
import sys
import oled_gfx_lib as g

if __name__ == "__main__":
    for desig in (sys.argv[1:] or ["LV-426"]):
        print(f"### seed token: {desig}")
        g.describe(desig)
