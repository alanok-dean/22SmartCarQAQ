import math, random, csv
random.seed(1)
rows=[]
dt=0.01
ang=0.0
for i in range(6000):
    t=i*dt
    # 静止 1s 后缓慢抬到 30 度再回 0
    if t<1: ang=0
    elif t<4: ang=(t-1)/3*30
    elif t<8: ang=30
    elif t<11: ang=30-(t-8)/3*30
    else: ang=0
    rate=0.0
    if 1<=t<4 or 8<=t<11:
        rate=10.0
    gy=rate+10.4+random.gauss(0,0.1)   # 带 10.4 dps 零偏
    ax=-math.sin(math.radians(ang))+random.gauss(0,0.01)
    az=math.cos(math.radians(ang))+random.gauss(0,0.01)
    rows.append([t,ax,random.gauss(0,0.01),az,0,gy,0])
with open('_selftest.csv','w',newline='') as f:
    w=csv.writer(f); w.writerow(['t','ax','ay','az','gx','gy','gz']); w.writerows(rows)
print('generated', len(rows))
