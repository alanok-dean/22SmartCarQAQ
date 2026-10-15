1.运行方式
把legacy_corr.py,sample_data.csv,config.yaml放在同一个文件夹下就可以运行了可喜可贺

2.输入格式 
yaml有三个字段input_csv: "sample_data.csv"是去这个csv里读取文件，x: "sensor_a"/y: "sensor_b"分别是读取这个csv里的两列数据并分别作为column x与y，为之后导入到xs和ys列表做准备
csv需要三列，第一列是时间戳（非必要），第二列是sensor_a，第三列是sensor_b，这两列不能变，代表两个传感器，分别要传到x和y

3.输出含义
n代表的是总行数，mean代表的是该列所有值的平均值，r代表的是这两列数据的相关系数，r大于0说明两列数据成正相关，反之说明负相关，等于1或-1说明线性相关。|r|越接近1，说明二者相关程度越强，越接近0，相关程度越弱