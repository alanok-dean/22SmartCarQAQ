#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sensor_analyzer.py  —— 上一届学长留下的"能用"的脚本

注释（学长原话）：
    "处理一下传感器数据就能用"

原本意图：
    1. 读取 sensor_data.csv（列：time, value）
    2. 计算 value 的平均值、标准差
    3. 剔除离群值（|value - mean| > 2 * std）
    4. 把清洗后的数据保存为 cleaned_data.csv
    5. 打印一份统计摘要

现状：跑不通 / 跑出来数不对。就交给你了。
"""

import csv
import os
import sys
import stat

INPUT_FILE = "sensor_data.csv"#输入文件
OUTPUT_FILE = "cleaned_data.csv"#输出文件
OUTPUT_DIR = "out"  # 输出目录

data = []
times = []
cleaned = []

print("=== 传感器数据分析 ===")

# --- 读取数据 ---
try:
    reader = csv.DictReader(open(INPUT_FILE, "r"))
except FileNotFoundError as x:
    print("找不到sensor_data.csv文件，请重试。错误代码为",x)
    sys.exit(1)

try :           #检查列名
    for row in reader:
        t = float(row["time"])
        v = float(row["value"])    ###value打错成大写了
        times.append(t)
        data.append(v)
except KeyError :
    print ("sensor_data.csv列名错误，请检查")
    sys.exit(1)

a = bool(times)
b = bool(data)
if a == 0 :
    print ("sensor_data.csv文件中time列存在0值，请重试")
    sys.exit(1)
elif b == 0 :
    print ("sensor_data.csv文件中value列存在0值，请重试")
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
std = acc**0.5 / len(data)

# --- 剔除离群值 ---
for v in data:
    if abs (v-mean) > 2 * std: ###离群值计算也应为绝对值，此处错误
        data.remove(v)

# --- 输出清洗后的数据 ---
output_path = os.path.join("/", OUTPUT_DIR, OUTPUT_FILE)
os.chmod(OUTPUT_DIR,stat.S_IXGRP)
os.chmod(output_path,stat.S_IXGRP)
os.chmod(OUTPUT_FILE,stat.S_IXGRP)
f = "cleaned_data.csv"
f = open(output_path, "w")
writer = csv.writer(f)
writer.writerow(["time", "value"])
for v in cleaned:
    writer.writerow([v])

print("均值 mean = %.4f" % mean)
print("标准差 std = %.4f" % std)
print("清洗后剩余 %d 条" % len(cleaned))
print("已保存到 %s" % output_path)
