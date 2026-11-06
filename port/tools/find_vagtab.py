import struct
d = open("/tmp/vh.bin","rb").read()
def u16(o): return d[o] | (d[o+1]<<8)
VS = 27; fsize = 296944
# The VAG offset table: ~VS entries of small sizes/8, rest zero; sum*8 ~= VB size.
# Scan every 2-byte offset for a plausible table.
best = []
for o in range(32, len(d)-256*2, 2):
    vals = [u16(o+i*2) for i in range(VS+2)]
    # entry 0 often 0; entries 1..VS positive & "small"; entry VS+1 == 0
    body = vals[1:VS+1]
    if all(0 < v < 20000 for v in body) and vals[VS+1] == 0:
        s = sum(body)*8
        best.append((abs(s-(fsize)), o, s, body[:6]))
best.sort()
for diff,o,s,sample in best[:6]:
    print("off=%d  bodysum*8=%d  first6=%s" % (o, s, sample))
print("--- candidate fixed offsets ---")
for o in (2944, 15904, 2080+16*27*32, 67616):
    print("off=%d  u16[0..5]=%s" % (o, [u16(o+i*2) for i in range(6)]))
