#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sensor_analyzer.py  —— 上一届学长留下的"能用"的脚本

注释（学长原话）：
    "处理一下传感器数据就能用"

原本意图：
    1. 读取 sensor_data.csv(列:time, value)
    2. 计算 value 的平均值、标准差
    3. 剔除离群值（|value - mean| > 2 * std)
    4. 把清洗后的数据保存为 cleaned_data.csv
    5. 打印一份统计摘要

现状：跑不通 / 跑出来数不对。就交给你了。
"""

import csv
import os
import sys
import stat
import argparse
parser = argparse.ArgumentParser(description="M0-3")#导入数据
parser.add_argument("--input",type=str,help="输入路径")
parser.add_argument("--output",type=str,default="cleaned_data.csv",help="输出路径")
args = parser.parse_args()

if  args.input :
    print("显式指定输入输出中")
    INPUT_FILE = args.input  #输入文件
    OUTPUT_FILE = args.output#输出文件
else :
    print("默认输入输出中")
    INPUT_FILE = "sensor_data.csv"  #输入文件
    OUTPUT_FILE = "cleaned_data.csv"#输出文件
# OUTPUT_DIR = "out"            #输出目录,但是不知道有什么用，反正都要输出到当前工作目录

data = []
times = []
cleaned = []

print("=== 传感器数据分析 ===")

# --- 读取数据 ---
try:#避免文件不存在崩溃
    reader = csv.DictReader(open(INPUT_FILE, "r"))#读取输入文件
except FileNotFoundError as x:
    print("找不到sensor_data.csv文件，请重试。错误代码为",x)
    sys.exit(1)

try :#检查列名并将数据输入到列表中
    for row in reader:
        t = float(row["time"])
        v = float(row["value"])    ###value打错成大写了
        times.append(t)
        data.append(v)
except KeyError as q:
    print ("sensor_data.csv列名错误,请检查",q)
    sys.exit(1)
except Exception as p:
    print("请检查sensor_data.csv内容",p)
    sys.exit(1)

if len(data) == 0:#检测是否空文件
    print ("请检查sensor_data.csv是否为空")
    sys.exit(1)
elif len(times) == 0:
    print ("请检查sensor_data.csv是否为空")
    sys.exit(1)

print("共读取 %d 条数据" % len(data))

# --- 计算平均值 ---
total = 0
for v in data:
    total += v
mean = total / len(data)

# --- 计算标准差 ---
acc = 0
for v in data:
    acc += (v - mean)**2   ###此处标准差应先平方后开方，即取绝对值之和
std = acc / len(data) - 1
std = std**0.5             ###标准差计算公式也错了

# --- 剔除离群值 ---
for i in range(len(data)-1,-1,-1) :
    if abs (data[i]-mean) > 2 * std: ###离群值计算也应为绝对值，此处错误
        data.pop(i)
        times.pop(i)

cleaned = data    ###这里没有将v重新输入到cleaned里
cleaned_time = times

# --- 输出清洗后的数据 ---
current_dir = os.getcwd() # 获取当前工作目录
output_path = os.path.join("/",current_dir,OUTPUT_FILE)###此处文件路径错误

with open(output_path, "w") as f:  ###此处文件写入函数时使用错误/且路径不需要引号，应为变量
    writer = csv.writer(f)
    writer.writerow(["time","value"])
    for a,b in zip(times,data):
        writer.writerow([round(a,2),b])   #此处用ai问了一下怎么让两组数据一一对应，即zip函数

print("均值 mean = %.4f" % mean)
print("标准差 std = %.4f" % std)
print("清洗后剩余 %d 条" % len(cleaned))
print("已保存到 %s" % output_path)