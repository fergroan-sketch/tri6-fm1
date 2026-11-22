# writes app/tests/demo.txt: every factory patch played in its own way, one WAV each (PRESETS.md gives the categories)
import re
pat = re.compile(r'^\{"([^"]+)", \{([^}]*)\}, (-?\d+), (-?\d+), (\d+),', re.M)
bank = [(m.group(1), [int(x) for x in m.group(2).split(",")], int(m.group(5)))
        for m in pat.finditer(open("app/factory.h").read())]
cat, cats = None, []
for l in open("PRESETS.md"):
    if l.startswith("## "): cat = l[3:].strip()
    elif l.startswith("- "): cats.append(cat)
assert len(cats) == len(bank)
K = open("tools/gen_factory.py").read()
K = re.search(r'K = \((.*?)\)\.split\(\)', K, re.S).group(1)
K = "".join(re.findall(r'"([^"]*)"', K)).split()
AS, ARP = K.index("as"), K.index("arp")
CHORDS = [[0x30, 0x37, 0x3C, 0x40, 0x43], [0x2D, 0x34, 0x39, 0x3C, 0x40], [0x29, 0x30, 0x35, 0x39, 0x3C]]
LINE = [0x3C, 0x3E, 0x40, 0x43, 0x45, 0x43, 0x40, 0x3E, 0x3C]
o = ["# the factory patches, each played: app/build/host/tri6fm1_host app/tests/demo.txt OUTDIR",
     "# written by tools/gen_demo.py", "wait 600", "master 3600", "midi B0 10 00", "wait 30"]
def play(notes, on, off=150, morph=False):
    for n in notes: o.append("midi 90 %02X 64" % n)
    if morph:                            # MORPH from 0 up to 127 and back while the notes hold
        for k in list(range(0, 128, 8)) + list(range(127, -1, -8)):
            o.extend(["midi B0 10 %02X" % k, "wait %d" % (on // 32)])
    else:
        o.append("wait %d" % on)
    for n in notes: o.append("midi 80 %02X 00" % n)
    o.append("wait %d" % off)
for i, (name, v, nchord) in enumerate(bank):
    c = cats[i]
    if i: o += ["turn PRESET 1", "wait 30"]
    o += ["expect patch %d" % i, "wav %03d-%s.wav" % (i, name.lower().replace(" ", "-").replace("+", ""))]
    if v[ARP]:
        play([0x30, 0x33, 0x37, 0x3A], 3200, 400, c == "MORPH and motion")
    elif c == "Bass":
        for k in (0x24, 0x24, 0x30, 0x27, 0x2B, 0x29, 0x24): play([k], 220, 60)
        o.append("wait 400")
    elif c == "Leads":
        for k in LINE: play([k], 260, 20)
        o.append("wait 300")
    elif nchord:
        for k in (0x30, 0x2D, 0x29, 0x2B): play([k], 600)
    elif c == "MORPH and motion":
        play(CHORDS[0], 3200, 300, True)
    elif v[AS] == 0:
        for ch in CHORDS: play(ch, 350, 250)
    else:
        for ch in CHORDS: play(ch, 1300)
    o += ["wait 1800", "wavstop"]
open("app/tests/demo.txt", "w").write("\n".join(o) + "\n")
print(len(bank))
