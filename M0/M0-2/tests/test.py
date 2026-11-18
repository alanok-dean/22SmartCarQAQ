import legacy_corr 
xs = [1,2,3]#[1,2,3] 与 [2,4,6] 的相关系数应为 1.0
ys = [6,4,2]
r = legacy_corr.correlation(xs,ys)
print (round(r,0))

aa = [1,1,1]#测试零方差
bb = [1,2,3]
r1 = legacy_corr.correlation(aa,bb)
print (r1)