"""Fit the S2 half-pipe lag model (trilogy_s2ss.c pass_overruns).
Inputs: scratch/ss/ref/log.txt (scratch/ssref_seq.py 3000 0, the real cartridge) and scratch/lag.log
(TRILOGY_S2SS=1 TRILOGY_SSLOG=1 TRILOGY_SSNOLAG=1, our per-pass work). Aligns passes, compares work."""
import re, collections
ref=[]
for l in open('scratch/ss/ref/log.txt'):
    m=re.match(r'(\d+) gm=(\w+) idx=(\d+) mf=(\w+) seg=(\d+)',l); ref.append((int(m[4],16),int(m[3]),int(m[5])))
runs=[]
for r in ref:
    if runs and runs[-1][0]==r: runs[-1][1]+=1
    else: runs.append([r,1])
ours=[]
for l in open('scratch/lag.log'):
    m=re.match(r'\[ss\] ph=(\d+) idx=(\d+) mf=(\w+) seg=(\d+).*cost=([\d,]+)',l)
    if m and m[1] in ('6','7'): ours.append(((int(m[3],16),int(m[2]),int(m[4])),[int(x) for x in m[5].split(',')]))
print(len(runs),len(ours))
# align: find offset maximizing match of key seq
rk=[r[0] for r in runs]; ok=[o[0] for o in ours]
best=None
for off in range(-200,200):
    n=sum(1 for i in range(len(ok)) if 0<=i+off<len(rk) and rk[i+off]==ok[i])
    if not best or n>best[0]: best=(n,off)
print('best',best)
off=best[1]
data=[]
mm=[]
for i,(k,c) in enumerate(ours):
    j=i+off
    if 0<=j<len(runs) and runs[j][0]==k: data.append((k,c,runs[j][1]))
    else: mm.append(i)
print('mismatch idx ranges', mm[:10], mm[-10:])
import itertools
for idx in range(5):
    sub=[d for d in data if d[0][1]==idx]
    lag=[d for d in sub if d[2]==2]; nol=[d for d in sub if d[2]==1]
    avg=lambda L,f: sum(f(d) for d in L)/max(1,len(L))
    print(idx, len(lag), len(nol), 'unc',round(avg(lag,lambda d:d[1][0])),round(avg(nol,lambda d:d[1][0])),
      'rle',round(avg(lag,lambda d:d[1][1])),round(avg(nol,lambda d:d[1][1])),'words',round(avg(lag,lambda d:d[1][2])),round(avg(nol,lambda d:d[1][2])),
      'objs',round(avg(lag,lambda d:d[1][4]),1),round(avg(nol,lambda d:d[1][4]),1),'spr',round(avg(lag,lambda d:d[1][5]),1),round(avg(nol,lambda d:d[1][5]),1),
      'flip',round(avg(lag,lambda d:d[1][3]),2),round(avg(nol,lambda d:d[1][3]),2))
import pickle; pickle.dump(data,open('scratch/lagdata.pkl','wb'))
