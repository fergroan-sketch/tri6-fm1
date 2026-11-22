# TRI-6 FM-1 factory bank generator: writes app/factory.h and PRESETS.md. Every patch is designed for this firmware;
# none copies another instrument's presets. Keys are host/render.c's short names; waves as T S P R joined by +.
import sys
K = ("o1oct o1wave o1pw o1lvl o2oct o2wave o2pw o2lvl sync o3oct o3wave o3pw o3lvl o3key noise fdrv cut res fenv "
     "pole2 fkey fvel fa fd fs fr aa ad as ar aenv avel lfreq lshape lamt lsync lpitch lpw lfilt lamp pm3 pmf pmpitch "
     "pmpw pmfb pmfilt dist vint pan vol glide grate gmode bend uni keymode fx fxa amix ap1 ap2 async fxb bmix bp1 bp2 "
     "bsync arp amode aoct adiv bpm ahold").split()
assert len(K) == 73
BASE = dict(o1oct=2, o1wave="S", o1pw=64, o1lvl=100, o2oct=2, o2wave="S", o2pw=64, o2lvl=0, sync=0, o3oct=3,
            o3wave="S", o3pw=64, o3lvl=0, o3key=1, noise=0, fdrv=64, cut=80, res=10, fenv=64, pole2=0, fkey=1,
            fvel=0, fa=0, fd=60, fs=60, fr=40, aa=0, ad=60, as_=110, ar=40, aenv=127, avel=1, lfreq=60, lshape=0,
            lamt=0, lsync=0, lpitch=0, lpw=0, lfilt=0, lamp=0, pm3=64, pmf=64, pmpitch=0, pmpw=0, pmfb=0, pmfilt=0,
            dist=0, vint=20, pan=40, vol=100, glide=0, grate=40, gmode=0, bend=2, uni=0, keymode=2, fx=1, fxa=0,
            amix=50, ap1=64, ap2=64, async_=0, fxb=0, bmix=30, bp1=64, bp2=40, bsync=0, arp=0, amode=0, aoct=0,
            adiv=6, bpm=120, ahold=0)
FX = dict(off=0, bbd=1, ddl=2, cho=3, ph1=4, ph2=5, ph3=6, ring=7, fl1=8, fl2=9, hall=10, room=11, plate=12,
          spring=13)
WAVE = dict(T=1, S=2, P=4, R=8)
CHORD_FLAG = "chord"
# the mod matrix: m1..m8=SOURCE:DEST:AMOUNT (amount -63..63), in the order of tri6.h's MS_* and MD_*
MSRC = "off lfo fenv aenv vel modw press key rand bend osc3 morph".split()
MDST = ("off pitch pitch1 pitch2 pitch3 pw1 pw2 pw3 lvl1 lvl2 lvl3 noise cutoff res fdrv amp pan lforate lfoamt amix "
        "bmix fenv attack decay ap1 bp1").split()
NMM = 8

def val(k, v):
    if k.endswith("wave"):
        return sum(WAVE[c] for c in v.split("+")) if isinstance(v, str) else v
    if k in ("fxa", "fxb") and isinstance(v, str):
        return FX[v]
    return int(v)

B = []
SPEC = {}
def P(name, s, cat):
    SPEC[name] = s
    assert len(name) <= 11, name
    d = {k.rstrip("_"): v for k, v in BASE.items()}
    extra = dict(p2=0, p3=0, chord=[], mm=[[0, 0, 64] for _ in range(NMM)])
    for t in s.split():
        k, v = t.split("=")
        if k in d: d[k] = v
        elif k in ("p2", "p3"): extra[k] = int(v)
        elif k == CHORD_FLAG: extra["chord"] = [int(x) for x in v.split(",")]
        elif k[0] == "m" and k[1:].isdigit() and 1 <= int(k[1:]) <= NMM:
            a, b, c = v.split(":")
            assert -63 <= int(c) <= 63, name
            extra["mm"][int(k[1:]) - 1] = [MSRC.index(a), MDST.index(b), 64 + int(c)]
        else: raise SystemExit("bad key %s in %s" % (k, name))
    vals = [val(k, d[k]) for k in K]
    if extra["chord"]:
        vals[K.index("uni")] = 7
    B.append((name, vals, extra, cat))

C = "INIT"
P("INIT", "cut=127 res=0 fkey=0 vint=0 pan=0 fx=0 fs=127", C)
C = "Brass"
P("BRASS SECT", "o2lvl=90 p2=8 o1lvl=95 cut=52 res=12 fenv=96 fa=52 fd=78 fs=70 fr=58 aa=40 ad=80 as=115 ar=60 fvel=1 vint=35 pan=60 fxb=hall bmix=26 bp1=60 bp2=50 m1=press:cutoff:25 m2=vel:attack:-20", C)
P("SYNTH HORN", "o1wave=S+P o1pw=40 o2lvl=85 p2=-6 cut=46 res=20 fenv=100 fa=40 fd=70 fs=55 fr=50 aa=30 ad=70 as=110 ar=50 fvel=1 vint=30 fxa=cho amix=35 ap1=40 ap2=60 fxb=room bmix=22", C)
P("SOFT BRASS", "o2lvl=80 p2=5 cut=40 res=8 fenv=88 fa=70 fd=85 fs=80 fr=70 aa=64 ad=85 as=118 ar=70 fvel=1 vint=40 fxb=hall bmix=30", C)
P("BRASS SWELL", "o2lvl=90 p2=10 o3lvl=60 o3oct=2 cut=36 res=18 fenv=92 fa=95 fd=95 fs=100 fr=80 aa=90 ad=90 as=120 ar=85 vint=45 pan=70 fxa=cho amix=30 fxb=hall bmix=35 bp1=80", C)
P("BRIGHT STAB", "o1wave=S+P o1pw=50 o2lvl=95 p2=12 o3lvl=80 o3oct=4 cut=62 res=25 fenv=100 fd=58 fs=30 fr=40 ad=62 as=60 ar=40 fvel=1 dist=20 fxb=plate bmix=25 bp1=50", C)
P("SYNC BRASS", "o1lvl=70 o2lvl=100 o2oct=3 sync=1 p2=300 pmf=84 pmpitch=2 cut=60 res=15 fenv=90 fa=30 fd=75 fs=60 fr=50 aa=25 ad=75 as=110 ar=55 vint=25 fxb=room bmix=25", C)
C = "Pads"
P("WARM PAD", "o2lvl=90 p2=9 o3lvl=70 o3oct=2 p3=-7 cut=50 res=15 fenv=78 fa=90 fd=100 fs=90 fr=95 aa=92 ad=100 as=120 ar=98 vint=50 pan=90 lfreq=40 lamt=10 lpw=7 fxa=cho amix=40 ap1=30 ap2=70 fxb=hall bmix=40 bp1=90 bp2=40 m1=rand:pan:40", C)
P("GLASS PAD", "o1wave=T+P o1pw=30 o2wave=T o2oct=3 o2lvl=70 p2=4 cut=78 res=40 fenv=74 fa=80 fd=90 fs=60 fr=90 aa=80 ad=90 as=110 ar=100 vint=35 pan=100 fxa=ph2 amix=50 ap1=20 ap2=80 fxb=plate bmix=45 bp1=95 bp2=60", C)
P("DARK PAD", "o1oct=1 o2lvl=90 p2=-8 o3lvl=60 o3wave=T cut=36 res=25 fenv=72 fa=100 fd=100 fs=100 fr=100 aa=100 ad=100 as=120 ar=105 vint=55 pan=80 fxb=hall bmix=45 bp1=100", C)
P("SLOW BLOOM", "o2lvl=90 p2=7 cut=28 res=30 fenv=100 fa=112 fd=110 fs=80 fr=100 aa=96 ad=100 as=120 ar=100 vint=40 pan=90 lfreq=35 lamt=12 lfilt=1 fxa=cho amix=40 fxb=hall bmix=40 bp1=95", C)
P("PWM PAD", "o1wave=P o2wave=P o2lvl=95 p2=6 cut=58 res=12 fenv=70 lfreq=45 lamt=50 lpw=3 lshape=0 fa=85 fd=95 fs=90 fr=90 aa=85 ad=95 as=120 ar=95 vint=40 pan=100 fxa=cho amix=45 ap1=35 ap2=80 fxb=hall bmix=35", C)
P("SPACE PAD", "o1wave=S+T o2wave=T o2oct=3 o2lvl=60 p2=702 o3lvl=50 noise=10 cut=60 res=45 fenv=80 fa=100 fd=110 fs=60 fr=110 aa=100 ad=100 as=120 ar=110 lfreq=20 lamt=30 lfilt=1 vint=45 pan=127 fxa=fl2 amix=40 ap1=10 ap2=90 fxb=hall bmix=55 bp1=110 bp2=70", C)
P("DRIFT PAD", "o2lvl=90 p2=14 o3lvl=80 p3=-12 cut=54 res=20 fenv=74 fa=90 fd=100 fs=85 fr=95 aa=95 ad=100 as=120 ar=100 vint=100 pan=110 fxa=bbd amix=25 ap1=70 ap2=50 fxb=hall bmix=40 bp1=95 m1=rand:cutoff:8", C)
P("CRYSTAL PAD", "o1wave=T o1oct=3 o2wave=P o2pw=20 o2lvl=80 p2=1200 sync=1 o3lvl=0 pmf=90 pmpitch=2 cut=88 res=30 fenv=70 fa=90 fd=95 fs=40 fr=95 aa=80 ad=90 as=110 ar=100 vint=30 pan=120 fxa=cho amix=40 fxb=plate bmix=50 bp1=100 bp2=70", C)
C = "Strings"
P("STRINGS", "o1wave=S o2lvl=95 p2=10 o3lvl=70 o3oct=4 p3=-8 cut=64 res=8 fenv=72 fa=70 fd=90 fs=90 fr=80 aa=75 ad=90 as=120 ar=80 lfreq=58 lamt=10 lpitch=7 vint=50 pan=110 fxa=cho amix=55 ap1=45 ap2=90 fxb=hall bmix=30 m1=press:cutoff:20", C)
P("SLOW STRING", "o2lvl=95 p2=12 cut=56 res=6 fenv=70 fa=95 fd=100 fs=90 fr=95 aa=100 ad=100 as=120 ar=100 lfreq=55 lamt=12 lpitch=7 vint=55 pan=110 fxa=cho amix=50 fxb=hall bmix=40 bp1=95", C)
P("ENSEMBLE", "o1wave=P o1pw=36 o2wave=S o2lvl=90 p2=-9 cut=70 res=5 fenv=70 fa=60 fd=90 fs=95 fr=80 aa=65 ad=90 as=120 ar=85 lfreq=48 lamt=40 lpw=1 vint=60 pan=127 fxa=cho amix=64 ap1=55 ap2=110 fxb=room bmix=30", C)
P("CELLO SECT", "o1oct=1 o2oct=1 o2lvl=90 p2=7 cut=48 res=15 fenv=76 fa=60 fd=85 fs=80 fr=70 aa=70 ad=85 as=120 ar=70 lfreq=60 lamt=12 lpitch=7 vint=45 fxb=hall bmix=30", C)
C = "Keys"
P("SOFT KEYS", "o1wave=T o2wave=S o2lvl=40 p2=5 cut=60 res=10 fenv=84 fd=82 fs=20 fr=70 ad=95 as=0 ar=72 fvel=1 vint=25 pan=60 fxa=cho amix=40 fxb=room bmix=25", C)
P("BELL KEYS", "o1wave=T o2wave=T o2oct=4 o2lvl=60 p2=-20 o3lvl=0 o3oct=4 o3wave=T pm3=88 pmpitch=1 cut=90 res=10 fenv=74 fd=80 fs=10 fr=80 ad=96 as=0 ar=85 vint=20 pan=80 fxa=cho amix=30 fxb=plate bmix=35 bp1=80", C)
P("CLAV", "o1wave=P o1pw=24 o2wave=P o2pw=100 o2lvl=60 p2=3 cut=62 res=35 fenv=100 fd=46 fs=10 fr=30 ad=60 as=20 ar=28 fvel=1 fkey=2 vint=15 fxa=ph2 amix=35 ap1=50 ap2=70 m1=vel:cutoff:10", C)
P("POLY PIANO", "o1wave=S+T o2wave=P o2pw=30 o2lvl=70 p2=4 cut=58 res=5 fenv=96 fd=74 fs=15 fr=60 ad=100 as=0 ar=66 fvel=1 fkey=2 vint=20 pan=70 fxb=room bmix=30 bp1=70 m1=vel:cutoff:10 m2=key:decay:-25", C)
P("VIBES", "o1wave=T o2wave=T o2oct=4 o2lvl=35 cut=100 res=0 fenv=64 fd=60 ad=94 as=0 ar=88 lfreq=72 lamt=50 lamp=1 vint=10 pan=60 fxb=plate bmix=30", C)
P("ORGAN", "o1wave=T+S o2wave=T o2oct=3 o2lvl=80 o3wave=T o3oct=5 o3lvl=60 cut=96 res=0 fkey=2 fenv=64 aa=4 ad=60 as=127 ar=20 aenv=127 avel=0 vint=15 fxa=ph3 amix=40 ap1=40 ap2=70 fxb=room bmix=20", C)
C = "Bass"
P("FAT BASS", "o1oct=1 o2oct=1 o2lvl=95 p2=7 o3oct=2 o3lvl=80 o3wave=P cut=40 res=30 fenv=96 fd=62 fs=30 fr=36 ad=70 as=110 ar=30 uni=1 keymode=2 vint=20 pan=0 fx=0", C)
P("SUB BASS", "o1oct=0 o1wave=T o2oct=1 o2wave=P o2lvl=50 cut=34 res=10 fenv=80 fd=60 fs=40 fr=30 ad=60 as=120 ar=26 uni=1 vint=10 pan=0 fx=0", C)
P("ACID BASS", "o1oct=1 o1wave=S cut=36 res=100 fenv=104 fd=56 fs=0 fr=30 ad=70 as=110 ar=24 pole2=0 fdrv=80 uni=1 glide=1 grate=58 gmode=1 dist=30 vint=10 pan=0 fx=1 fxa=ddl amix=20 ap1=60 ap2=40 async=1", C)
P("UNISON BASS", "o1oct=1 o2oct=1 o2lvl=95 p2=12 cut=44 res=20 fenv=94 fd=66 fs=40 fr=40 ad=70 as=115 ar=34 uni=6 vint=60 pan=90 fx=0", C)
P("PLUCK BASS", "o1oct=1 o1wave=P o1pw=40 o2oct=0 o2wave=T o2lvl=70 cut=38 res=40 fenv=100 fd=50 fs=0 fr=40 ad=74 as=0 ar=40 fvel=1 fkey=1 uni=1 vint=10 pan=0 fx=0", C)
P("FM BASS", "o1oct=1 o1wave=T o3oct=2 o3wave=T o3lvl=0 pm3=104 pmpitch=1 pmf=40 cut=70 res=10 fenv=86 fd=56 fs=20 fr=30 ad=70 as=110 ar=30 uni=1 vint=10 pan=0 fx=0", C)
P("DRIVE BASS", "o1oct=1 o2oct=1 o2lvl=90 p2=-10 o2wave=P o2pw=50 cut=46 res=40 fenv=92 fd=64 fs=40 fr=36 ad=70 as=115 ar=30 fdrv=110 dist=60 uni=2 vint=20 pan=0 fx=0", C)
P("WOBBLE BASS", "o1oct=1 o2oct=1 o2lvl=95 p2=6 cut=40 res=70 fenv=64 lfreq=64 lsync=2 lamt=100 lfilt=1 lshape=0 ad=60 as=120 ar=30 fdrv=90 dist=40 uni=2 vint=15 pan=0 fx=0 bpm=140 m1=modw:lforate:30", C)
C = "Leads"
P("SYNC LEAD", "o1lvl=40 o2lvl=100 o2oct=3 sync=1 p2=0 pmf=96 pmpitch=2 cut=86 res=20 fenv=80 fd=80 fs=80 fr=50 ad=80 as=120 ar=46 uni=2 glide=1 grate=60 vint=30 pan=40 lfreq=70 lamt=14 lpitch=3 fxa=ddl amix=25 ap1=70 ap2=50 async=1 fxb=hall bmix=20 m1=press:lfoamt:40 m2=modw:pitch2:30", C)
P("SAW LEAD", "o2lvl=95 p2=12 o3lvl=80 o3oct=4 cut=80 res=25 fenv=82 fd=70 fs=80 fr=50 ad=80 as=120 ar=46 uni=3 glide=1 grate=50 gmode=1 vint=40 pan=60 lfreq=70 lamt=14 lpitch=7 fxa=bbd amix=25 ap1=75 ap2=50 fxb=hall bmix=20 m1=press:cutoff:30", C)
P("SQUARE LEAD", "o1wave=P o2wave=P o2oct=1 o2lvl=60 pole2=1 cut=74 res=30 fenv=80 fd=70 fs=70 fr=50 as=120 ar=46 uni=1 glide=1 grate=45 lfreq=66 lamt=16 lpitch=3 vint=20 pan=0 fxa=ddl amix=25 ap1=80 ap2=45 fxb=room bmix=20 m1=press:lfoamt:40", C)
P("SCREAM LEAD", "o1wave=S+P o2lvl=95 p2=7 cut=70 res=90 fenv=90 fd=80 fs=70 fr=50 as=120 ar=46 fdrv=100 dist=70 uni=2 glide=1 grate=50 lfreq=72 lamt=18 lpitch=7 vint=30 pan=0 fxb=hall bmix=22", C)
P("GLIDE LEAD", "o1wave=S o2wave=T o2oct=3 o2lvl=60 cut=84 res=20 fenv=74 as=120 ar=50 uni=1 glide=1 grate=64 gmode=0 lfreq=64 lamt=12 lpitch=7 vint=20 pan=0 fxa=ddl amix=30 ap1=64 ap2=60 async=1 fxb=hall bmix=25", C)
P("FEEDBACK LD", "o1wave=S o2lvl=80 p2=5 cut=72 res=40 fenv=80 fdrv=12 fd=80 fs=80 as=120 ar=46 uni=1 glide=1 grate=50 vint=25 pan=0 fxa=ddl amix=25 ap1=70 ap2=55 fxb=room bmix=20", C)
P("SOLO FLUTE", "o1wave=T o2wave=T o2oct=3 o2lvl=30 noise=18 pole2=1 cut=66 res=20 fenv=74 fa=50 fd=80 fs=70 aa=50 ad=80 as=118 ar=50 lfreq=66 lamt=14 lpitch=7 uni=1 glide=1 grate=40 vint=20 pan=0 fxb=hall bmix=30 m1=press:lfoamt:35 m2=vel:noise:20", C)
P("PM LEAD", "o1wave=S o3wave=S o3oct=4 o3lvl=40 p3=700 pm3=84 pmpitch=1 pmfilt=1 cut=70 res=30 fenv=84 fd=74 fs=70 as=120 ar=46 uni=1 glide=1 grate=50 vint=25 pan=0 fxa=ddl amix=25 fxb=hall bmix=20", C)
C = "Plucks and mallets"
P("PLUCK", "o1wave=S+P o1pw=40 o2lvl=80 p2=7 cut=46 res=30 fenv=100 fd=52 fs=0 fr=50 ad=78 as=0 ar=60 fvel=1 vint=25 pan=90 fxa=cho amix=30 fxb=plate bmix=30 bp1=70", C)
P("MALLET", "o1wave=T o2wave=T o2oct=4 o2lvl=40 p2=-1200 o3wave=T o3oct=5 o3lvl=0 pm3=82 pmpitch=1 cut=86 fenv=80 fd=56 fs=0 fr=60 ad=74 as=0 ar=70 fvel=1 vint=15 pan=70 fxb=room bmix=30", C)
P("KALIMBA", "o1wave=T o1oct=3 o3wave=T o3oct=5 p3=340 pm3=90 pmpitch=1 cut=90 res=10 fenv=74 fd=50 fs=0 fr=50 ad=66 as=0 ar=66 fvel=1 vint=20 pan=80 fxb=plate bmix=35 bp1=70", C)
P("HARP", "o1wave=T+S o2wave=T o2oct=3 o2lvl=50 cut=62 res=10 fenv=96 fd=70 fs=0 fr=80 ad=90 as=0 ar=86 fvel=1 fkey=2 vint=20 pan=110 fxa=cho amix=30 fxb=hall bmix=40 bp1=85 m1=key:decay:-25", C)
P("STEEL DRUM", "o1wave=T o2wave=T o2lvl=60 p2=700 sync=0 o3wave=T o3oct=4 p3=386 pm3=92 pmpitch=3 cut=88 res=20 fenv=78 fd=56 fs=0 fr=56 ad=74 as=0 ar=60 fvel=1 vint=15 pan=60 fxb=room bmix=30", C)
P("MARIMBA", "o1wave=T o1oct=2 o2wave=T o2oct=4 o2lvl=25 cut=74 res=20 fenv=84 fd=48 fs=0 fr=46 ad=62 as=0 ar=50 fvel=1 vint=10 pan=70 fxb=room bmix=25", C)
C = "Poly mod and effects"
P("METAL BELL", "o1wave=T o2wave=T o2lvl=60 p2=-500 o3wave=S o3oct=5 p3=213 pm3=100 pmpitch=3 cut=100 res=10 fenv=70 fd=90 fs=0 fr=96 ad=104 as=0 ar=100 vint=10 pan=100 fxb=hall bmix=45 bp1=90 bp2=60", C)
P("CHAOS", "o1wave=S+P o2lvl=80 p2=600 o3oct=0 o3wave=S+P o3key=0 p3=500 pm3=110 pmf=100 pmpitch=7 pmpw=3 pmfb=1 pmfilt=1 fdrv=20 cut=70 res=60 fenv=90 fa=60 fd=100 fs=60 fr=90 aa=40 ad=90 as=110 ar=90 vint=60 pan=127 fxa=fl1 amix=50 ap1=30 ap2=90 fxb=hall bmix=40 bp1=90 m1=rand:pitch3:30", C)
P("SWEEP FX", "o1wave=S o2lvl=90 p2=15 noise=30 cut=20 res=90 fenv=110 fa=110 fd=110 fs=0 fr=110 aa=100 ad=100 as=120 ar=110 lfreq=20 lamt=60 lfilt=1 vint=40 pan=127 fxa=ph1 amix=60 ap1=20 ap2=100 fxb=hall bmix=50 bp1=110", C)
P("LASER", "o1wave=S o1oct=3 o3oct=0 o3wave=R o3key=0 pmf=120 pmpitch=1 cut=90 res=50 fenv=64 fd=60 fs=0 fr=40 ad=74 as=0 ar=50 vint=10 pan=60 fxa=ddl amix=35 ap1=50 ap2=60 async=1 fxb=room bmix=30", C)
P("NOISE WIND", "o1lvl=0 noise=110 cut=60 res=100 fenv=64 fkey=2 lfreq=25 lshape=4 lamt=70 lfilt=1 aa=100 ad=100 as=120 ar=110 vint=60 pan=127 fxa=ph2 amix=40 ap1=10 ap2=100 fxb=hall bmix=50 bp1=100", C)
P("RING BELL", "o1wave=T o2wave=T o2lvl=50 p2=1200 cut=100 fenv=64 fd=90 fs=0 fr=96 ad=100 as=0 ar=100 vint=10 pan=90 fxa=ring amix=70 ap1=90 ap2=100 fxb=plate bmix=45 bp1=90", C)
C = "Chords"
P("CHORD STAB", "o1wave=S+P o1pw=40 o2lvl=90 p2=10 cut=58 res=30 fenv=96 fd=56 fs=10 fr=46 ad=70 as=40 ar=46 fvel=1 vint=40 pan=110 chord=0,7,12,15,19 fxa=cho amix=35 fxb=plate bmix=30", C)
P("MINOR 9", "o1wave=S o2wave=P o2lvl=70 p2=6 cut=50 res=20 fenv=86 fa=40 fd=80 fs=40 fr=70 aa=20 ad=80 as=110 ar=70 vint=40 pan=110 chord=0,3,7,10,14 fxa=cho amix=40 fxb=hall bmix=35", C)
P("FIFTHS", "o1wave=S o2lvl=80 p2=8 cut=48 res=40 fenv=96 fd=70 fs=30 fr=50 ad=80 as=100 ar=50 fdrv=84 vint=30 pan=100 chord=0,7,12 fxb=room bmix=30", C)
C = "Arpeggios"
P("ARP PLUCK", "o1wave=S+P o1pw=40 cut=44 res=40 fenv=100 fd=48 fs=0 fr=40 ad=60 as=0 ar=40 vint=20 pan=80 arp=1 amode=0 aoct=1 adiv=6 bpm=118 fxa=ddl amix=30 ap1=64 ap2=50 async=1 fxb=hall bmix=25", C)
P("ARP SEQ", "o1wave=S o2lvl=70 p2=1200 cut=40 res=70 fenv=100 fd=50 fs=0 fr=30 ad=60 as=0 ar=30 vint=15 pan=40 arp=1 amode=2 aoct=1 adiv=6 bpm=124 fxa=bbd amix=25 ap1=70 ap2=40 async=1", C)
P("ARP BELLS", "o1wave=T o3wave=T o3oct=5 p3=700 pm3=86 pmpitch=1 cut=90 fenv=70 fd=56 fs=0 fr=60 ad=66 as=0 ar=66 vint=10 pan=100 arp=1 amode=0 aoct=2 adiv=7 bpm=110 fxa=ddl amix=35 ap1=50 ap2=60 async=1 fxb=plate bmix=35", C)
P("ARP BASS", "o1oct=1 o2oct=1 o2lvl=80 p2=7 cut=38 res=60 fenv=100 fd=46 fs=0 fr=30 ad=56 as=0 ar=30 vint=10 pan=0 arp=1 amode=2 aoct=0 adiv=6 bpm=126 fx=0", C)
P("ARP DRIVE", "o1oct=2 o1wave=S+P o2lvl=80 p2=-12 cut=46 res=50 fenv=96 fd=52 fs=20 fr=30 ad=60 as=60 ar=30 fdrv=110 dist=50 vint=15 pan=40 arp=1 amode=1 aoct=1 adiv=8 bpm=120 fxa=ddl amix=20 ap1=60 ap2=40 async=1", C)
P("ARP SYNC", "o1lvl=40 o2lvl=100 o2oct=3 sync=1 pmf=100 pmpitch=2 cut=80 res=20 fenv=86 fd=50 fs=0 fr=40 ad=60 as=0 ar=40 vint=15 pan=60 arp=1 amode=0 aoct=1 adiv=4 bpm=112 fxa=ddl amix=25 ap1=70 ap2=50 async=1 fxb=room bmix=25", C)
P("ARP RANDOM", "o1wave=T+P o1pw=30 cut=70 res=30 fenv=90 fd=50 fs=0 fr=60 ad=60 as=0 ar=60 vint=25 pan=110 arp=1 amode=3 aoct=2 adiv=6 bpm=120 fxa=ddl amix=35 ap1=60 ap2=60 async=1 fxb=hall bmix=35 m1=rand:cutoff:8 m2=rand:pan:50", C)
P("ARP DREAM", "o1wave=S o2wave=T o2oct=3 o2lvl=60 cut=60 res=30 fenv=86 fa=20 fd=80 fs=30 fr=80 aa=10 ad=80 as=60 ar=80 vint=40 pan=120 lfreq=30 lamt=30 lfilt=1 arp=1 amode=4 aoct=1 adiv=5 bpm=96 fxa=cho amix=40 fxb=hall bmix=45 bp1=100", C)
C = "Voices"
P("CHOIR", "o1wave=P o1pw=50 o2wave=P o2pw=30 o2lvl=80 p2=8 noise=8 pole2=1 cut=58 res=50 fenv=70 fa=90 fd=90 fs=90 fr=90 aa=90 ad=90 as=118 ar=90 lfreq=50 lamt=40 lpw=3 vint=60 pan=120 fxa=cho amix=55 ap1=40 ap2=90 fxb=hall bmix=45 bp1=95", C)
P("VOCAL PAD", "o1wave=S o2wave=S o2lvl=80 p2=-7 cut=54 res=80 pole2=1 fenv=70 fkey=2 fa=80 fd=95 fs=90 fr=90 aa=80 ad=90 as=118 ar=90 lfreq=40 lamt=26 lfilt=1 vint=55 pan=120 fxa=cho amix=50 fxb=hall bmix=45 m1=modw:cutoff:20", C)

C = "Mod matrix"
P("TWO LFOS", "o1wave=P o1pw=50 o2wave=S o2lvl=80 p2=7 o3oct=0 o3wave=T o3lvl=0 o3key=0 p3=-500 cut=56 res=35 fenv=74 fa=60 fd=90 fs=80 fr=80 aa=60 ad=90 as=120 ar=85 lfreq=58 lamt=8 lpitch=3 vint=30 pan=100 fxa=cho amix=40 fxb=hall bmix=35 bp1=90 m1=osc3:pw1:30 m2=osc3:cutoff:10 m3=vel:fenv:15 m4=rand:pan:30", C)
P("VELO BRASS", "o2lvl=90 p2=8 cut=44 res=14 fenv=92 fa=50 fd=78 fs=70 fr=58 aa=50 ad=80 as=115 ar=60 vint=35 pan=60 fxb=hall bmix=26 bp1=60 bp2=50 m1=vel:cutoff:30 m2=vel:attack:-40 m3=vel:fdrv:12 m4=morph:cutoff:30", C)
P("PRESS PAD", "o2lvl=90 p2=9 o3lvl=60 o3oct=2 p3=-7 cut=46 res=15 fenv=76 fa=90 fd=100 fs=90 fr=95 aa=92 ad=100 as=120 ar=98 vint=50 pan=90 lfreq=58 lpitch=7 fxa=cho amix=40 ap1=30 ap2=70 fxb=hall bmix=40 bp1=90 m1=press:cutoff:35 m2=press:lfoamt:24 m3=modw:lfoamt:30 m4=morph:bmix:40", C)
P("KEY PIANO", "o1wave=S+T o2wave=P o2pw=30 o2lvl=70 p2=4 cut=56 res=5 fenv=96 fd=84 fs=15 fr=60 ad=108 as=0 ar=66 fkey=2 vint=20 pan=70 fxb=room bmix=30 bp1=70 m1=key:decay:-30 m2=vel:cutoff:28 m3=key:pan:30 m4=morph:cutoff:25", C)
P("OSC3 WOBBLE", "o1wave=P o1pw=50 o2lvl=85 p2=7 o3oct=0 o3key=0 o3lvl=0 p3=-300 cut=50 res=50 fenv=70 fd=70 fs=60 ad=70 as=120 ar=40 lfreq=40 vint=20 pan=50 fxa=cho amix=30 fxb=room bmix=25 m1=osc3:cutoff:30 m2=lfo:pw1:25 m3=lfo:pw2:-25 m4=morph:res:30", C)
P("RAND PLUCK", "o1wave=S+P o1pw=40 o2lvl=80 p2=7 cut=46 res=30 fenv=100 fd=52 fs=0 fr=50 ad=78 as=0 ar=60 fvel=1 vint=25 pan=0 fxa=cho amix=30 fxb=plate bmix=30 bp1=70 m1=rand:cutoff:24 m2=rand:pan:60 m3=rand:decay:20 m4=morph:decay:40", C)
P("S+H SWEEP", "o1wave=S o2lvl=85 p2=10 o3oct=0 o3key=0 o3lvl=0 p3=200 cut=56 res=70 fenv=64 lfreq=74 lshape=4 ad=60 as=120 ar=50 vint=20 pan=60 fxa=ddl amix=30 ap1=64 ap2=55 async=1 fxb=room bmix=25 m1=lfo:cutoff:32 m2=osc3:res:14 m3=lfo:pan:30 m4=morph:lforate:30", C)
P("ENV PWM KEY", "o1wave=P o1pw=60 o2wave=P o2pw=40 o2lvl=60 p2=5 cut=66 res=10 fenv=84 fd=80 fs=20 fr=70 ad=95 as=0 ar=72 vint=25 pan=60 fxa=cho amix=40 fxb=room bmix=25 m1=fenv:pw1:40 m2=fenv:pw2:-35 m3=vel:decay:30 m4=vel:cutoff:20 m5=morph:pw1:-30", C)
P("BEND GROWL", "o1wave=S+P o2lvl=95 p2=7 cut=64 res=50 fenv=86 fd=80 fs=70 fr=50 as=120 ar=46 fdrv=80 uni=2 glide=1 grate=50 vint=30 pan=0 bend=2 fxb=room bmix=20 m1=bend:fdrv:40 m2=bend:cutoff:24 m3=bend:res:16 m4=morph:fdrv:35", C)
P("WHEEL MORPH", "o1wave=S o2wave=P o2pw=30 o2lvl=0 p2=1200 o3lvl=0 o3oct=4 cut=48 res=25 fenv=74 fa=80 fd=95 fs=90 fr=90 aa=85 ad=95 as=120 ar=95 lfreq=50 lamt=10 lpw=2 vint=45 pan=100 fxa=cho amix=45 fxb=hall bmix=35 m1=modw:lvl2:60 m2=modw:lvl3:40 m3=modw:cutoff:25 m4=modw:lforate:30 m5=morph:lvl2:60", C)
P("PRESS LEAD", "o2lvl=95 p2=12 o3lvl=60 o3oct=4 cut=74 res=25 fenv=82 fd=70 fs=80 fr=50 ad=80 as=120 ar=46 uni=3 glide=1 grate=50 gmode=1 vint=40 pan=60 lfreq=70 lpitch=7 fxa=bbd amix=25 ap1=75 ap2=50 fxb=hall bmix=20 m1=press:lfoamt:40 m2=press:cutoff:22 m3=aenv:noise:8 m4=morph:lfoamt:30", C)
P("SLOW ATTACK", "o2lvl=90 p2=9 cut=52 res=15 fenv=80 fa=40 fd=100 fs=90 fr=95 aa=40 ad=100 as=120 ar=98 vint=50 pan=90 fxa=cho amix=40 fxb=hall bmix=40 bp1=90 m1=vel:attack:-50 m2=key:attack:-20 m3=rand:pitch2:10 m4=vel:amp:20 m5=morph:attack:50", C)
P("MORPH PAD", "o1wave=S o2wave=T o2oct=3 o2lvl=0 p2=5 o3lvl=0 cut=42 res=20 fenv=74 fa=80 fd=95 fs=90 fr=95 aa=80 ad=95 as=120 ar=95 lfreq=40 lamt=8 lpw=1 vint=45 pan=100 fxa=cho amix=40 fxb=hall bmix=25 bp1=95 m1=morph:cutoff:30 m2=morph:lvl2:55 m3=morph:bmix:35 m4=morph:lforate:20 m5=morph:attack:-20 m6=vel:cutoff:15", C)

# the 49 patches that fill the bank to 128 (0.3), by category; each uses what TRI-6's engine has: three oscillators,
# sync, poly mod, feedback / drive, the 2 / 4 pole ladder, vintage, unison, the matrix and MORPH, two effects
NEW = {
"Brass": [
 ("BRASS STACK", "o1wave=S o2lvl=95 p2=9 o3oct=3 o3lvl=80 p3=-11 cut=48 res=16 fenv=94 fa=48 fd=80 fs=70 fr=56 aa=40 ad=80 as=118 ar=58 fvel=1 uni=3 keymode=2 vint=40 pan=90 fxa=cho amix=25 fxb=hall bmix=28 bp1=70"),
 ("PM BRASS", "o2lvl=90 p2=5 o3oct=4 o3wave=T o3lvl=0 pmf=78 pmpitch=2 cut=50 res=18 fenv=92 fa=44 fd=76 fs=68 fr=55 aa=36 ad=78 as=115 ar=58 fvel=1 vint=30 pan=60 fxb=room bmix=24"),
 ("BRASS 2POLE", "o1wave=S+P o1pw=48 o2lvl=85 p2=7 pole2=1 cut=46 res=30 fenv=96 fa=40 fd=78 fs=60 fr=55 aa=30 ad=78 as=115 ar=55 fvel=1 vint=35 pan=70 fxb=plate bmix=22 bp1=70"),
 ("FANFARE", "o2lvl=90 p2=6 o2oct=3 o3lvl=70 o3oct=3 p3=700 cut=60 res=20 fenv=100 fa=30 fd=70 fs=75 fr=60 aa=24 ad=75 as=118 ar=60 fvel=1 vint=30 pan=80 fxb=hall bmix=30 bp1=80"),
 ("MUTED HORN", "o1wave=S+T o2lvl=70 p2=4 pole2=1 cut=34 res=12 fenv=84 fkey=2 fa=60 fd=85 fs=75 fr=65 aa=50 ad=85 as=115 ar=65 fvel=1 vint=30 pan=40 fxb=room bmix=26 m1=vel:cutoff:28"),
],
"Pads": [
 ("TRIGON PAD", "o1wave=T+S o2wave=T+S o2lvl=90 p2=8 o3wave=R o3lvl=70 p3=-9 cut=58 res=20 fenv=72 fa=90 fd=100 fs=90 fr=100 aa=90 ad=100 as=120 ar=100 lfreq=36 lamt=10 lpw=7 vint=70 pan=120 fxa=cho amix=40 ap1=30 ap2=70 fxb=hall bmix=42 bp1=100"),
 ("FDBK PAD", "o1wave=S o2lvl=85 p2=7 fdrv=34 cut=52 res=35 fenv=74 fa=95 fd=100 fs=90 fr=100 aa=95 ad=100 as=120 ar=100 lfreq=28 lamt=20 lfilt=1 vint=50 pan=110 fxa=ph1 amix=35 ap1=20 ap2=70 fxb=hall bmix=40 bp1=100"),
 ("SYNC PAD", "o1lvl=50 o2lvl=95 o2oct=3 sync=1 p2=300 cut=64 res=20 fenv=70 fa=90 fd=100 fs=90 fr=100 aa=90 ad=100 as=120 ar=100 vint=40 pan=110 fxa=cho amix=40 fxb=plate bmix=45 bp1=100 m1=lfo:pitch2:30 m2=osc3:pitch2:12 o3oct=0 o3key=0 o3lvl=0 lfreq=22"),
 ("AIR PAD", "o1wave=T o2wave=T o2oct=3 o2lvl=50 noise=40 cut=70 res=60 fenv=70 fkey=2 fa=100 fd=100 fs=90 fr=110 aa=100 ad=100 as=120 ar=110 lfreq=20 lamt=30 lfilt=1 vint=40 pan=127 fxa=cho amix=20 fxb=hall bmix=55 bp1=110 bp2=70"),
 ("ICE PAD", "o1wave=T o1oct=3 o2wave=P o2pw=20 o2lvl=60 p2=1200 o3wave=R o3oct=4 o3lvl=50 p3=5 cut=80 res=30 fenv=70 fa=80 fd=90 fs=60 fr=100 aa=80 ad=90 as=115 ar=105 vint=35 pan=120 fxa=fl1 amix=30 ap1=15 ap2=60 fxb=plate bmix=50 bp1=105"),
],
"Strings": [
 ("STRING MACH", "o1wave=S+P o1pw=40 o2wave=S o2lvl=90 p2=6 o2oct=3 cut=72 res=4 fenv=64 fa=70 fd=90 fs=95 fr=85 aa=72 ad=90 as=120 ar=88 lfreq=50 lamt=36 lpw=1 vint=55 pan=127 fxa=cho amix=70 ap1=60 ap2=110 fxb=hall bmix=30"),
 ("VIOLINS", "o1wave=S o1oct=3 o2lvl=90 o2oct=3 p2=9 cut=70 res=10 fenv=74 fkey=2 fa=60 fd=90 fs=90 fr=80 aa=66 ad=90 as=120 ar=80 lfreq=60 lamt=0 lpitch=3 vint=50 pan=110 fxa=cho amix=40 fxb=hall bmix=34 m1=aenv:lfoamt:20 m2=modw:lfoamt:40"),
 ("BOW STRINGS", "o2lvl=95 p2=8 o3lvl=60 o3oct=2 cut=58 res=8 fenv=72 fa=80 fd=95 fs=90 fr=90 aa=82 ad=95 as=120 ar=92 lfreq=56 lamt=10 lpitch=7 vint=55 pan=110 fxa=cho amix=50 fxb=hall bmix=38 m1=vel:attack:-40 m2=vel:cutoff:20"),
 ("PIZZICATO", "o1wave=S o2lvl=85 p2=7 cut=54 res=10 fenv=96 fkey=2 fd=58 fs=0 fr=50 ad=70 as=0 ar=60 fvel=1 vint=35 pan=110 fxb=hall bmix=34 bp1=80"),
 ("OCTAVE STRS", "o1wave=S o2wave=S o2oct=3 o2lvl=85 p2=6 o3oct=2 o3lvl=70 p3=-6 cut=62 res=6 fenv=70 fa=70 fd=90 fs=90 fr=85 aa=74 ad=90 as=120 ar=86 lfreq=54 lamt=12 lpitch=7 vint=55 pan=120 fxa=cho amix=50 fxb=hall bmix=34"),
 ("DARK STRING", "o1oct=1 o2oct=2 o2lvl=90 p2=8 pole2=1 cut=44 res=10 fenv=70 fa=85 fd=95 fs=90 fr=95 aa=85 ad=95 as=120 ar=98 lfreq=50 lamt=10 lpitch=7 vint=60 pan=110 fxa=cho amix=45 fxb=hall bmix=42 bp1=100"),
],
"Keys": [
 ("EP TINE", "o1wave=T o3wave=T o3oct=5 o3lvl=0 p3=0 pm3=82 pmpitch=1 cut=80 res=0 fenv=74 fd=86 fs=10 fr=70 ad=104 as=0 ar=76 fvel=1 vint=20 pan=70 lfreq=70 lamt=20 lamp=1 fxa=cho amix=30 fxb=room bmix=25 m1=vel:pitch1:4"),
 ("WURLY", "o1wave=P o1pw=56 o2wave=T o2lvl=50 fdrv=84 cut=60 res=10 fenv=84 fd=80 fs=20 fr=60 ad=96 as=0 ar=66 fvel=1 vint=20 pan=60 lfreq=66 lamt=30 lamp=1 fxa=ph3 amix=25 ap1=30 ap2=60 fxb=room bmix=22"),
 ("HARPSICHORD", "o1wave=P o1pw=24 o2wave=S o2oct=3 o2lvl=60 p2=3 cut=84 res=20 fenv=90 fkey=2 fd=60 fs=10 fr=50 ad=80 as=0 ar=50 fvel=0 vint=10 pan=40 fxb=room bmix=24"),
 ("PERC ORGAN", "o1wave=T o2wave=T o2oct=3 o2lvl=80 o3wave=T o3oct=5 o3lvl=50 cut=100 res=0 fkey=2 fenv=80 fd=40 fs=60 aa=0 ad=60 as=127 ar=18 avel=0 vint=15 fxa=ph3 amix=35 ap1=60 ap2=70 fxb=room bmix=20 m1=aenv:lvl3:30"),
 ("SYNC KEYS", "o1lvl=40 o2lvl=95 o2oct=3 sync=1 pmf=96 pmpitch=2 cut=72 res=15 fenv=84 fd=74 fs=10 fr=60 ad=94 as=0 ar=66 fvel=1 vint=20 pan=60 fxa=cho amix=30 fxb=room bmix=25"),
 ("TOY PIANO", "o1wave=T o1oct=3 o3wave=T o3oct=5 o3lvl=0 p3=500 pm3=96 pmpitch=1 cut=96 res=0 fenv=70 fd=56 fs=0 fr=50 ad=74 as=0 ar=60 fvel=1 vint=25 pan=60 fxb=plate bmix=30 bp1=60"),
],
"Bass": [
 ("SYNC BASS", "o1oct=1 o1lvl=50 o2oct=2 o2lvl=100 sync=1 pmf=100 pmpitch=2 cut=50 res=20 fenv=96 fd=60 fs=30 fr=34 ad=70 as=110 ar=30 uni=1 vint=10 pan=0 fx=0"),
 ("GROWL BASS", "o1oct=1 o1wave=S+P o2oct=1 o2lvl=90 p2=-8 fdrv=104 cut=44 res=50 fenv=90 fd=64 fs=40 fr=34 ad=70 as=115 ar=30 lfreq=72 lamt=24 lfilt=1 uni=1 dist=35 vint=15 pan=0 fx=0"),
 ("REESE BASS", "o1oct=1 o2oct=1 o2lvl=100 p2=22 o3oct=2 o3lvl=60 p3=-18 cut=48 res=20 fenv=74 fd=70 fs=80 fr=34 ad=70 as=118 ar=34 uni=2 vint=40 pan=30 fdrv=80 fx=0"),
 ("FUNK BASS", "o1oct=1 o1wave=P o1pw=30 o2oct=1 o2lvl=60 cut=46 res=50 fenv=100 fd=48 fs=0 fr=30 ad=60 as=60 ar=26 fvel=1 uni=1 vint=10 pan=0 fx=0"),
 ("DUB BASS", "o1oct=0 o1wave=T o2oct=1 o2wave=S o2lvl=30 pole2=1 cut=30 res=20 fenv=74 fd=70 fs=60 fr=40 ad=80 as=118 ar=40 uni=1 glide=1 grate=40 gmode=1 vint=15 pan=0 fx=1 fxa=bbd amix=15 ap1=70 ap2=40 async=1"),
],
"Leads": [
 ("OCTAVE LEAD", "o2lvl=90 o2oct=3 p2=6 o3lvl=70 o3oct=2 cut=78 res=22 fenv=80 fd=72 fs=80 fr=50 ad=80 as=120 ar=46 uni=1 glide=1 grate=48 vint=25 pan=0 lfreq=70 lamt=14 lpitch=7 fxa=ddl amix=25 ap1=70 ap2=50 async=1 fxb=hall bmix=20"),
 ("PWM LEAD", "o1wave=P o2wave=P o2lvl=90 p2=8 cut=76 res=20 fenv=78 fd=72 fs=80 fr=50 as=120 ar=46 lfreq=62 lamt=50 lpw=3 uni=1 glide=1 grate=46 vint=20 pan=0 fxa=cho amix=30 fxb=room bmix=20"),
 ("SYNC SWEEP", "o1lvl=30 o2lvl=100 o2oct=3 sync=1 pmf=110 pmpitch=2 cut=88 res=24 fenv=80 fa=30 fd=100 fs=60 fr=50 ad=80 as=120 ar=46 uni=2 glide=1 grate=55 vint=30 pan=40 fxa=ddl amix=25 ap1=70 ap2=55 async=1 fxb=hall bmix=22 m1=modw:pitch2:30"),
 ("WHISTLE", "o1wave=T o1oct=3 o2lvl=0 noise=10 pole2=1 cut=90 res=40 fenv=70 fkey=2 fa=40 fd=80 fs=80 aa=40 ad=80 as=120 ar=50 lfreq=68 lamt=16 lpitch=7 uni=1 glide=1 grate=36 vint=15 pan=0 fxb=hall bmix=30"),
 ("DRIVE LEAD", "o1wave=S+P o2lvl=95 p2=10 fdrv=110 cut=66 res=40 fenv=84 fd=80 fs=70 fr=50 as=120 ar=46 dist=40 uni=2 glide=1 grate=50 vint=25 pan=20 lfreq=70 lamt=14 lpitch=7 fxa=ddl amix=20 ap1=70 ap2=45 async=1 fxb=room bmix=18"),
],
"Plucks and mallets": [
 ("NYLON", "o1wave=T+S o2wave=P o2pw=40 o2lvl=40 cut=58 res=10 fenv=94 fkey=2 fd=72 fs=0 fr=70 ad=96 as=0 ar=80 fvel=1 vint=20 pan=60 fxb=room bmix=28 bp1=70"),
 ("BELL PLUCK", "o1wave=T o3wave=T o3oct=5 o3lvl=0 p3=386 pm3=88 pmpitch=1 cut=88 fenv=78 fd=86 fs=0 fr=90 ad=104 as=0 ar=96 fvel=1 vint=15 pan=90 fxa=cho amix=25 fxb=plate bmix=40 bp1=95"),
 ("MUSIC BOX", "o1wave=T o1oct=4 o2wave=T o2oct=4 o2lvl=40 p2=10 cut=100 fenv=64 fd=70 fs=0 fr=70 ad=92 as=0 ar=80 vint=20 pan=80 fxa=cho amix=20 fxb=plate bmix=40 bp1=90"),
 ("SYNC PLUCK", "o1lvl=40 o2lvl=95 o2oct=3 sync=1 pmf=110 pmpitch=2 cut=70 res=20 fenv=96 fd=56 fs=0 fr=50 ad=74 as=0 ar=56 fvel=1 vint=20 pan=80 fxa=ddl amix=25 ap1=60 ap2=40 async=1 fxb=room bmix=25"),
],
"Poly mod and effects": [
 ("DRONE", "o1oct=1 o1wave=S o2oct=1 o2lvl=90 p2=702 o3oct=0 o3key=0 o3lvl=0 p3=-600 pm3=70 pmpw=1 cut=50 res=50 fenv=64 aa=110 ad=100 as=127 ar=120 lfreq=12 lamt=30 lfilt=1 vint=80 pan=127 fxa=ph2 amix=40 ap1=10 ap2=90 fxb=hall bmix=55 bp1=115 m1=osc3:cutoff:15"),
 ("RISER", "o1wave=S o2lvl=90 p2=12 noise=40 cut=20 res=80 fenv=120 fa=125 fd=100 fs=127 fr=90 aa=110 ad=100 as=127 ar=90 vint=30 pan=127 fxa=fl2 amix=50 ap1=10 ap2=90 fxb=hall bmix=50 bp1=110 m1=fenv:pitch:6 m2=fenv:lforate:30"),
 ("ALARM", "o1wave=P o1oct=3 o3oct=0 o3key=0 o3lvl=0 o3wave=S p3=500 cut=90 res=20 fenv=64 ad=60 as=127 ar=30 vint=0 pan=0 fxb=room bmix=20 m1=osc3:pitch1:40"),
 ("SCI-FI", "o1wave=T o1oct=3 o3oct=0 o3key=0 o3wave=S+P o3lvl=0 p3=200 cut=80 res=70 fenv=64 ad=80 as=120 ar=80 lfreq=90 lshape=4 vint=30 pan=100 fxa=ring amix=40 ap1=60 ap2=100 fxb=plate bmix=45 m1=osc3:pitch1:30 m2=lfo:cutoff:30 m3=rand:pan:50"),
],
"Chords": [
 ("MAJOR 7", "o1wave=S o2wave=T o2lvl=70 p2=6 cut=56 res=15 fenv=84 fa=30 fd=80 fs=50 fr=70 aa=20 ad=80 as=110 ar=70 vint=40 pan=110 chord=0,4,7,11 fxa=cho amix=40 fxb=hall bmix=35"),
 ("SUS4", "o1wave=S+P o1pw=40 o2lvl=85 p2=8 cut=56 res=25 fenv=92 fd=62 fs=20 fr=50 ad=74 as=50 ar=50 fvel=1 vint=35 pan=110 chord=0,5,7,12 fxa=cho amix=30 fxb=plate bmix=30"),
 ("POWER CHORD", "o1wave=S o2lvl=90 p2=10 fdrv=106 cut=56 res=20 fenv=86 fd=80 fs=70 fr=50 ad=80 as=118 ar=50 dist=50 vint=30 pan=60 chord=0,7,12 fxb=room bmix=24"),
 ("HOUSE STAB", "o1wave=S o2lvl=80 p2=5 o2wave=P cut=60 res=30 fenv=96 fd=54 fs=0 fr=44 ad=66 as=0 ar=50 fvel=1 vint=30 pan=100 chord=0,3,7,10 fxa=ddl amix=25 ap1=64 ap2=45 async=1 fxb=room bmix=25"),
 ("DUB CHORD", "o1wave=S+P o1pw=40 o2lvl=85 p2=6 cut=60 res=30 fenv=90 fd=66 fs=0 fr=60 ad=86 as=0 ar=80 vint=40 pan=90 chord=0,3,7 fxa=bbd amix=40 ap1=80 ap2=70 async=1 fxb=spring bmix=35"),
],
"Arpeggios": [
 ("ARP PWM", "o1wave=P o2wave=P o2lvl=80 p2=7 cut=60 res=30 fenv=90 fd=50 fs=0 fr=40 ad=60 as=0 ar=40 lfreq=40 lamt=50 lpw=3 vint=20 pan=80 arp=1 amode=2 aoct=1 adiv=6 bpm=118 fxa=cho amix=30 fxb=room bmix=25"),
 ("ARP TRIGON", "o1wave=T+S o2wave=T o2oct=3 o2lvl=60 o3wave=R o3lvl=50 p3=12 cut=66 res=40 fenv=90 fd=56 fs=0 fr=50 ad=66 as=0 ar=50 vint=40 pan=110 arp=1 amode=4 aoct=1 adiv=8 bpm=112 fxa=ddl amix=30 ap1=64 ap2=55 async=1 fxb=hall bmix=30 m1=rand:cutoff:18"),
],
"Voices": [
 ("AAH VOICES", "o1wave=S o2wave=S o2lvl=85 p2=-6 noise=6 pole2=1 cut=60 res=70 fenv=66 fkey=2 fa=70 fd=90 fs=90 fr=90 aa=74 ad=90 as=118 ar=90 lfreq=52 lamt=14 lpitch=7 vint=60 pan=120 fxa=cho amix=50 fxb=hall bmix=45 bp1=95"),
 ("OOH CHOIR", "o1wave=T o2wave=P o2pw=40 o2lvl=60 p2=7 noise=4 pole2=1 cut=46 res=60 fenv=66 fkey=2 fa=80 fd=95 fs=90 fr=95 aa=82 ad=95 as=118 ar=95 lfreq=50 lamt=12 lpitch=7 vint=65 pan=120 fxa=cho amix=55 fxb=hall bmix=50 bp1=100"),
],
}
# MORPH on every patch that has no route from it: a sensible move per kind (two routes, in slots 8 and 7)
CAT_MORPH = {
 "Brass": ["morph:cutoff:25", "morph:attack:-15"], "Pads": ["morph:cutoff:22", "morph:bmix:30"],
 "Strings": ["morph:cutoff:20", "morph:lfoamt:25"], "Keys": ["morph:cutoff:25", "morph:decay:25"],
 "Bass": ["morph:fdrv:35", "morph:cutoff:15"], "Leads": ["morph:lfoamt:30", "morph:fdrv:20"],
 "Plucks and mallets": ["morph:decay:35", "morph:cutoff:15"], "Poly mod and effects": ["morph:res:25", "morph:lforate:30"],
 "Chords": ["morph:cutoff:25", "morph:amix:25"], "Arpeggios": ["morph:cutoff:25", "morph:decay:25"],
 "Voices": ["morph:cutoff:20", "morph:bmix:30"], "Mod matrix": ["morph:cutoff:20"],
}

for cat, pats in NEW.items():
    for name, spec in pats:
        P(name, spec, cat)
# the bank by kind: each category's patches together, in the order the categories first come (Mod matrix last)
ORDER = list(dict.fromkeys(b[3] for b in B))
B.sort(key=lambda b: ORDER.index(b[3]))
for name, v, e, cat in B:
    if cat in CAT_MORPH and not any(m[0] == MSRC.index("morph") for m in e["mm"]):
        free = [i for i, m in enumerate(e["mm"]) if m[0] == 0 and m[1] == 0][::-1]   # from slot 8 down: 1.. stay free
        for r, i in zip(CAT_MORPH[cat], free):
            a, b, c = r.split(":")
            e["mm"][i] = [MSRC.index(a), MDST.index(b), 64 + int(c)]

# each patch's VOLUME, from the demo renders' loudness (tools/gen_demo.py), so the bank plays at one level
LEVEL = {"INIT": 87, "BRASS SECT": 92, "SYNTH HORN": 98, "SOFT BRASS": 94, "BRASS SWELL": 98, "BRIGHT STAB": 99, "SYNC BRASS": 94, "WARM PAD": 99, "GLASS PAD": 102, "DARK PAD": 104, "SLOW BLOOM": 102, "PWM PAD": 95, "SPACE PAD": 98, "DRIFT PAD": 97, "CRYSTAL PAD": 104, "STRINGS": 99, "SLOW STRING": 107, "ENSEMBLE": 95, "CELLO SECT": 92, "SOFT KEYS": 108, "BELL KEYS": 107, "CLAV": 106, "POLY PIANO": 98, "VIBES": 103, "ORGAN": 83, "FAT BASS": 106, "SUB BASS": 102, "ACID BASS": 100, "UNISON BASS": 89, "PLUCK BASS": 112, "FM BASS": 106, "DRIVE BASS": 77, "WOBBLE BASS": 79, "SYNC LEAD": 106, "SAW LEAD": 99, "SQUARE LEAD": 105, "SCREAM LEAD": 81, "GLIDE LEAD": 113, "FEEDBACK LD": 88, "SOLO FLUTE": 113, "PM LEAD": 113, "PLUCK": 112, "MALLET": 110, "KALIMBA": 112, "HARP": 109, "STEEL DRUM": 108, "MARIMBA": 112, "METAL BELL": 100, "CHAOS": 80, "SWEEP FX": 105, "LASER": 126, "NOISE WIND": 127, "RING BELL": 110, "CHORD STAB": 118, "MINOR 9": 100, "FIFTHS": 97, "ARP PLUCK": 127, "ARP SEQ": 126, "ARP BELLS": 127, "ARP BASS": 123, "ARP DRIVE": 89, "ARP SYNC": 127, "ARP RANDOM": 127, "ARP DREAM": 125, "CHOIR": 99, "VOCAL PAD": 104, "TWO LFOS": 92, "BRASS STACK": 101, "PM BRASS": 92, "BRASS 2POLE": 95, "FANFARE": 91, "MUTED HORN": 88, "TRIGON PAD": 97, "FDBK PAD": 97, "SYNC PAD": 107, "AIR PAD": 106, "ICE PAD": 97, "STRING MACH": 105, "VIOLINS": 102, "BOW STRINGS": 96, "PIZZICATO": 121, "OCTAVE STRS": 103, "DARK STRING": 108, "EP TINE": 104, "WURLY": 97, "HARPSICHORD": 107, "PERC ORGAN": 75, "SYNC KEYS": 114, "TOY PIANO": 119, "SYNC BASS": 107, "GROWL BASS": 79, "REESE BASS": 91, "FUNK BASS": 118, "DUB BASS": 110, "OCTAVE LEAD": 108, "PWM LEAD": 101, "SYNC SWEEP": 109, "WHISTLE": 116, "DRIVE LEAD": 83, "NYLON": 93, "BELL PLUCK": 88, "MUSIC BOX": 99, "SYNC PLUCK": 115, "DRONE": 108, "RISER": 115, "ALARM": 81, "SCI-FI": 111, "MAJOR 7": 107, "SUS4": 122, "POWER CHORD": 80, "HOUSE STAB": 118, "DUB CHORD": 123, "ARP PWM": 116, "ARP TRIGON": 119, "AAH VOICES": 103, "OOH CHOIR": 106, "VELO BRASS": 84, "PRESS PAD": 99, "KEY PIANO": 88, "OSC3 WOBBLE": 93, "RAND PLUCK": 121, "S+H SWEEP": 99, "ENV PWM KEY": 91, "BEND GROWL": 91, "WHEEL MORPH": 104, "PRESS LEAD": 97, "SLOW ATTACK": 91, "MORPH PAD": 99}
for i, (name, v, e, cat) in enumerate(B):
    if name in LEVEL and 'vol=' not in SPEC[name]:
        v[K.index('vol')] = LEVEL[name]
assert len(B) <= 128
with open("app/factory.h", "w") as f:
    f.write("/* written by tools/gen_factory.py: the factory bank, every patch made for this firmware */\n")
    for name, v, e, cat in B:
        ch = e["chord"] + [0] * (6 - len(e["chord"]))
        f.write('{"%s", {%s}, %d, %d, %d, {%s}, {%s}},\n' % (name, ", ".join(str(x) for x in v), e["p2"], e["p3"],
                len(e["chord"]), ", ".join(str(x) for x in ch), ", ".join("{%d, %d, %d}" % tuple(m) for m in e["mm"])))
WORD = dict(pitch="pitch", pitch1="osc 1 pitch", pitch2="osc 2 pitch", pitch3="osc 3 pitch", pw1="osc 1 width",
            pw2="osc 2 width", pw3="osc 3 width", lvl1="osc 1 level", lvl2="osc 2 level", lvl3="osc 3 level",
            noise="noise", cutoff="cutoff", res="resonance", fdrv="drive", amp="level", pan="pan", lforate="LFO rate",
            lfoamt="LFO amount", amix="FX A mix", bmix="FX B mix", fenv="filter env", attack="attack", decay="decay",
            ap1="FX A time", bp1="FX B time")
with open("PRESETS.md", "w") as f:
    f.write("# TRI-6 FM-1 factory bank\n\n%d patches, 000 to %03d, laid out by kind. Every patch was made for this "
            "firmware; none is copied from any instrument's presets. Every patch but INIT has a MORPH route (the "
            "ALGORITHM knob): the mod matrix patches their own, the others a move that suits their kind. Each patch's MORPH move is listed after its name.\n"
            % (len(B), len(B) - 1))
    last = None
    for i, (name, v, e, cat) in enumerate(B):
        if cat != last:
            f.write("\n## %s\n\n" % cat)
            last = cat
        mv = [("longer " if m[2] > 64 else "shorter ") + WORD[MDST[m[1]]] if MDST[m[1]] in ("attack", "decay") else
              "%s %s" % (WORD[MDST[m[1]]], "up" if m[2] > 64 else "down") for m in e["mm"]
              if m[0] == MSRC.index("morph") and m[2] != 64]
        f.write("- %03d %s%s\n" % (i, name, " (MORPH: %s)" % ", ".join(mv) if mv else ""))
print(len(B), "patches")
