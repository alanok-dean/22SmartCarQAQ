import yaml
import csv
import math

CONFIG_PATH = "config.yaml"

with open(CONFIG_PATH) as f:
    cfg = yaml.safe_load(f)

csv_path = cfg["input_csv"]
col_x = cfg["columns"]["x"]
col_y = cfg["columns"]["y"]  #导入csv中的传感器a，b的值

xs = []
ys = []

with open(csv_path) as f:
    reader = csv.DictReader(f)
    for row in reader:
        xs.append(float(row[col_x]))
        ys.append(float(row[col_y])) #导入x和y到列表里面

n = len(xs)#算长度

sum_x = 0.0
sum_y = 0.0
for i in range(n):
    sum_x = sum_x + xs[i]
    sum_y = sum_y + ys[i]  #计算总值，为下一步平均准备

mean_x = sum_x / n
mean_y = sum_y / n
#算平均数

dx = 0.0
dy = 0.0
prod = 0.0
def correlation(dx,dy,prod):
    for i in range(n):
        a = xs[i] - mean_x
        b = ys[i] - mean_y
        dx = dx + a * a
        dy = dy + b * b
        prod = prod + a * b

    denom = dx * dy
    r = prod / denom
    return r

r = correlation(0,0,0)

print("n =", n)#总长
print("mean_x =", mean_x)#x平均
print("mean_y =", mean_y)#y平均
print("r =", r)#相关系数
