可以显视输入输出，格式  python3 sensor_analyzer.py --input sensor_data.csv（输入的） --output cleaned_data.csv（输出的）
也支持默认输入输出 sensor_data.csv（入）  cleaned_data.csv（出）

| 问题出处 | 现象 | 原因 | 如何解决 |
| ----------- | ----------- | ----------- | ----------- |
| 37 | 无法输入数据value | v错误大写 | 改小写 |
| 52 | 标准差计算错误 | 无先平方再求和，直接求和 | 改平方 |
| 53 | 标准差计算错误 | 计算公式错误 | 改len(data) - 1 |
| 58 | 最终没有输出正确的time | 没有按顺序剥除对应的time | 用遍历和索引同时删除不合格的time与value |
| 56 | 离群值剥除错误 | 不是差值绝对值 | 改成绝对值比较 |
| 58 | 列表值输入错误 | 没有把新值输入到cleaned中 | 输入到cleaned |
| 61 | 文件输出路径错误 | 不是当前目录 | 用os.getcwd()获取当前目录 |
| 62 | csv写入函数错误 | 用open没有close | 改成with open函数了 |
| 65 | csv格式写入错误 | 没有输出time列 | 把time和value用zip打包并且分行输出 |