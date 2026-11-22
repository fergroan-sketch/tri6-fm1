# writes app/tests/demo.txt: every factory patch played in its own way, one WAV each
import re
names = re.findall(r'^\{"([^"]+)"', open("app/factory.h").read(), re.M)
CHORDS = [[0x30, 0x37, 0x3C, 0x40, 0x43], [0x2D, 0x34, 0x39, 0x3C, 0x40], [0x29, 0x30, 0x35, 0x39, 0x3C]]
LINE = [0x3C, 0x3E, 0x40, 0x43, 0x45, 0x43, 0x40, 0x3E, 0x3C]
o = ["# the factory patches, each played: app/build/host/tri6fm1_host app/tests/demo.txt OUTDIR",
     "# written by tools/gen_demo.py", "wait 600", "master 3600", "wait 30"]
def play(notes, on, off=150):
    for n in notes: o.append("midi 90 %02X 64" % n)
    o.append("wait %d" % on)
    for n in notes: o.append("midi 80 %02X 00" % n)
    o.append("wait %d" % off)
for i, name in enumerate(names):
    if i: o += ["turn PRESET 1", "wait 30"]
    o += ["expect patch %d" % i, "wav %03d-%s.wav" % (i, name.lower().replace(" ", "-"))]
    n = name.split()
    if n[0] == "ARP":
        play([0x30, 0x33, 0x37, 0x3A], 3000, 400)
    elif n[-1] == "BASS":
        for k in (0x24, 0x24, 0x30, 0x27, 0x2B, 0x29, 0x24): play([k], 220, 60)
        o.append("wait 400")
    elif n[-1] in ("LEAD", "LD", "FLUTE"):
        for k in LINE: play([k], 260, 20)
        o.append("wait 300")
    elif name in ("CHORD STAB", "MINOR 9", "FIFTHS"):
        for k in (0x30, 0x2D, 0x29, 0x2B): play([k], 600)
    elif n[-1] in ("PLUCK", "MALLET", "KALIMBA", "HARP", "DRUM", "MARIMBA", "CLAV", "STAB", "VIBES", "BELL", "LASER"):
        for c in CHORDS: play(c, 350, 250)
    else:
        for c in CHORDS: play(c, 1300)
    o += ["wait 1800", "wavstop"]
open("app/tests/demo.txt", "w").write("\n".join(o) + "\n")
print(len(names))
