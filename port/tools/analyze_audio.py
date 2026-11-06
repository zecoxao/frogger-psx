import sys, array
path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/audiotest.raw"
d = open(path, "rb").read()
n = len(d) // 2
a = array.array("h"); a.frombytes(d[:n*2])
mx = 0; nz = 0; sq = 0
for x in a:
    ax = x if x >= 0 else -x
    if ax > mx: mx = ax
    if x: nz += 1
    sq += x*x
rms = (sq / n) ** 0.5 if n else 0
print("frames_stereo:", n//2, "  seconds:", round(n/2/44100, 2))
print("max_amplitude:", mx, "  rms:", round(rms, 1))
print("nonzero_samples:", nz, "of", n, "(", round(100*nz/max(n,1), 2), "%)")
print("VERDICT:", "SOUND PRESENT" if mx > 50 else "SILENT")
