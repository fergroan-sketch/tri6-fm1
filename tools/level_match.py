# reads the demo WAVs (app/tests/demo.txt, rendered to a folder) and prints a LEVEL dict for tools/gen_factory.py
# that brings every patch to the bank's median loudness (VOLUME: 0.45 dB a step, 100 = 0 dB)
import sys, os, re, wave, array, math
d = sys.argv[1]
src = open("app/factory.h").read()
pat = re.compile(r'^\{"([^"]+)", \{([^}]*)\}', re.M)
K = re.search(r'K = \((.*?)\)\.split\(\)', open("tools/gen_factory.py").read(), re.S).group(1)
VOL = "".join(re.findall(r'"([^"]*)"', K)).split().index("vol")
bank = [(m.group(1), int(m.group(2).split(",")[VOL])) for m in pat.finditer(src)]
loud = {}
for i, (name, vol) in enumerate(bank):
    f = [x for x in os.listdir(d) if x.startswith("%03d-" % i)][0]
    w = wave.open(os.path.join(d, f))
    a = array.array("h", w.readframes(w.getnframes()))
    n = w.getframerate() // 20 * w.getnchannels()     # 50 ms blocks; the loudness is the loudest 30% of them,
    bl = sorted((sum(x * x for x in a[j:j + n]) / n for j in range(0, len(a) - n, n)), reverse=True)
    bl = bl[:max(1, len(bl) * 3 // 10)]                # so a pluck and a pad that sound as loud measure the same
    rms = math.sqrt(sum(bl) / len(bl)) if bl else 0
    peak = max(abs(x) for x in a) if a else 0
    loud[name] = (20 * math.log10(rms / 32768) if rms else -120, peak / 32768, vol)
med = sorted(v[0] for v in loud.values())[len(loud) // 2]
out = {}
for name, (db, pk, vol) in loud.items():
    nv = max(0, min(127, vol + round((med - db) / 0.45)))
    out[name] = nv
    if abs(db - med) > 1.5 or pk > 0.97 or db < -60:
        print("# %-12s %6.1f dB peak %.2f vol %d -> %d" % (name, db, pk, vol, nv), file=sys.stderr)
print("median %.1f dB" % med, file=sys.stderr)
print("LEVEL = {%s}" % ", ".join('"%s": %d' % (k, v) for k, v in out.items()))
