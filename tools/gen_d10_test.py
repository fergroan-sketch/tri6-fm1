# writes app/tests/d10.txt: a bank block saved by 0.3 (slots 000-031 in 0.3's order, CLAV at 021 edited), then a
# restart: every unedited slot must hold this release's factory patch, the edited one stays, no name twice
import re, subprocess, struct
src = subprocess.run(["git", "show", "refs/tags/v0.3:app/factory.h"], capture_output=True, text=True, check=True).stdout
R = re.findall(r'^\{"([^"]*)", \{([^}]*)\}, (-?\d+), (-?\d+), (\d+), \{([^}]*)\}, \{(.*)\}\},?$', src, re.M)
def patch(i, edit=False):
    name, p, p2, p3, cn, ch, mm = R[i]
    p = [int(x) for x in p.split(",")]
    if edit:
        p[16] = min(127, p[16] + 1)          # CUTOFF up one: the player's edit
    b = struct.pack("<hh", int(p2), int(p3)) + bytes(p) + name.encode()[:11].ljust(12, b"\0")
    b += bytes([int(cn)]) + bytes(int(c) & 0xFF for c in ch.split(","))
    for s, d, a in re.findall(r"\{(\d+), (\d+), (\d+)\}", mm):
        s, d, a = int(s) & 15, int(d) & 31, int(a) & 127
        if s == 0 and d == 0 and a == 0:
            a = 64
        b += struct.pack("<H", s | d << 4 | a << 9)
    assert len(b) == 112
    return b
L = ["# D10: a block saved by 0.3 keeps 0.3's order; at the next start its unedited slots get this release's patches",
     "wait 600"]
for i in range(32):
    b = patch(i, i == 21)
    L.append("sysex F0 7D 54 36 01 %02X " % i + " ".join("%02X %02X" % (x >> 4, x & 15) for x in b) + " F7")
L += ["wait 50", "expect slotfac26 0", "tap SAVE", "wait 300", "reboot", "wait 800",
      "# 31 slots past INIT, CLAV at 021 edited: 30 moved (001-006 are in place already but get this release's level)",
      "expect migrated 30", "expect slotfac21 0", "expect dupes 1"]
L += ["expect slotfac%d 1" % i for i in range(1, 32) if i != 21]
L += ["# a second start: nothing left to move", "tap SAVE", "wait 300", "reboot", "wait 800", "expect migrated 0",
      "expect slotfac26 1"]
open("app/tests/d10.txt", "w").write("\n".join(L) + "\n")
print(len(L), "lines")
