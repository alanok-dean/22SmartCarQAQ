import yaml
import csv
import math
import sys
import numpy

CONFIG_PATH = "config.yaml"

try:#检查config是否存在
    with open(CONFIG_PATH) as f:
        cfg = yaml.safe_load(f)
except FileNotFoundError as e:
    print("找不到config文件，请重试。错误代码为",e)
    sys.exit(1)
except Exception as d:
    print ("config存在其他错误，请重试。错误代码为",d)
    sys.exit(1)

try:#检查config是否为空
    csv_path = cfg["input_csv"]
except TypeError as d:
    print ("config为空。错误代码为",d)
    sys.exit(1)

col_x = cfg["columns"]["x"]
col_y = cfg["columns"]["y"]  #导入csv中的传感器a，b的值

xs = []
ys = []

try:#检查sample_data存在与列名问题
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            xs.append(float(row[col_x]))
            ys.append(float(row[col_y])) #导入x和y到列表里面
except FileNotFoundError as a:
    print ("sample_data.csv不存在")
except KeyError as a:
    print("sample_data中列名错误。不匹配列名为",a)
    sys.exit(1)


sum_x = 0.0
sum_y = 0.0
dx = 0.0
dy = 0.0
prod = 0.0
def correlation(xs,ys):
    n = len(xs)#算长度
    if n == 0:#检查sample_data为空问题
        print ("sample_data.csv为空文件")
        sys.exit(1)

    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    dx = 0.0
    dy = 0.0
    prod = 0.0
    for i in range(n):
        
        a = xs[i] - mean_x
        b = ys[i] - mean_y
        prod = prod + a*b
        dx = dx + a*a
        dy = dy + b*b
        
    denom = math.sqrt(dx) * math.sqrt(dy)
    try:
        r = prod / denom
    except ZeroDivisionError as x:
        print("除零错误，有一列数据取值恒定",x)
        sys.exit(1)
    return r

n = len(xs)
mean_x = sum(xs) / len(xs)
mean_y = sum(ys) / len(ys)

r = correlation(xs,ys)
r1 = numpy.corrcoef(xs,ys)
r2 = r1[0,1]
if r - r2 < 1e-6:
    print ("计算结果大致相同，误差小于1e-6")

if __name__ == "__main__":
    print("n =", n)#总长
    print("mean_x =", mean_x)#x平均
    print("mean_y =", mean_y)#y平均
    print("r =", r)#相关系数