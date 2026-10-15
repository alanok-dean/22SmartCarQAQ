import yaml
import csv
import math
import sys

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

n = len(xs)#算长度
sum_x = 0.0
sum_y = 0.0
for i in range(n):
    sum_x = sum_x + xs[i]
    sum_y = sum_y + ys[i]  #计算总值，为下一步平均准备

try:#检查sample_data为空问题
    mean_x = sum_x / n
    mean_y = sum_y / n#算平均数
except ZeroDivisionError :
    print ("sample_data.csv为空文件")
    sys.exit(1)

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
    try:
        r = prod / denom
    except ZeroDivisionError as a:
        print("除零错误，有一列数据取值恒定")
        sys.exit(1)
    return r

r = correlation(0,0,0)

print("n =", n)#总长
print("mean_x =", mean_x)#x平均
print("mean_y =", mean_y)#y平均
print("r =", r)#相关系数
