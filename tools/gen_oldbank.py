# writes app/oldbank.h: a fingerprint of every factory patch of the releases before 0.4's re-sort (0.2, 0.3), slot by
# slot, so 0.4 and later can tell a saved slot still holding that release's factory patch from one the player edited.
# The fingerprint (FNV-1a, 32 bits) covers the name, every parameter but VOLUME, the fine pitches and the chord: not
# the routes (0.3 converted them, 0.4 added MORPH's) nor the level (re-levelled since). app.c's patch_print computes the
# same thing.  Run from the repo root: python3 tools/gen_oldbank.py
import re, subprocess
TP_VOLUME, TP_N, NAME_LEN = 49, 73, 12
def rows(tag):
    src = subprocess.run(["git", "show", tag + ":app/factory.h"], capture_output=True, text=True, check=True).stdout
    out = []
    for m in re.finditer(r'^\{"([^"]*)", \{([^}]*)\}, (-?\d+), (-?\d+), (\d+), \{([^}]*)\}', src, re.M):
        p = [int(x) for x in m.group(2).split(",")]
        assert len(p) == TP_N
        out.append((m.group(1), p, int(m.group(3)), int(m.group(4)), int(m.group(5)), [int(x) for x in m.group(6).split(",")]))
    return out
def fnv(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h
def print_of(name, p, p2, p3, cn, ch):
    nm = name.encode()[:NAME_LEN - 1].ljust(NAME_LEN, b"\0")
    pp = list(p); pp[TP_VOLUME] = 0
    data = bytes(nm) + bytes(pp) + (p2 & 0xFFFF).to_bytes(2, "little") + (p3 & 0xFFFF).to_bytes(2, "little")
    data += bytes([cn]) + bytes(c & 0xFF for c in ch)
    return fnv(data)
TAGS = ["v0.2", "refs/tags/v0.3"]
with open("app/oldbank.h", "w") as f:
    f.write("/* written by tools/gen_oldbank.py: the factory patches of 0.2 and 0.3 (0.4 re-sorted the bank), slot by slot */\n")
    f.write("#define OLD_BANKS %d\n#define OLD_SLOTS 128\n" % len(TAGS))
    f.write("static const uint32_t OLD_PRINT[OLD_BANKS][OLD_SLOTS] = {\n")
    for t in TAGS:
        r = rows(t)
        hs = [print_of(*x) for x in r] + [0] * (128 - len(r))
        f.write("    {  /* %s: %d patches */\n" % (t.split("/")[-1], len(r)))
        for i in range(0, 128, 8):
            f.write("        " + ", ".join("0x%08Xu" % h for h in hs[i:i + 8]) + ",\n")
        f.write("    },\n")
    f.write("};\n")
print("ok")
