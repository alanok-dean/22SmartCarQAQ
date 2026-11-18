这个schedule通过读取yaml或者json文件，将任务及其数据作为类录入，再封装到词典里，并且读取timeout。
接着进行一系列的模拟判断，及时，循环，依赖等等操作，模拟真实情况，且任务每种状态都有特定鲜艳颜色效果，更加便于观察以及判断
任务会执行，成功则进入到下一个，失败则重新循环，且总尝试次数小于等于3，大于3则直接失败，且依赖于该任务的任务也会直接跳过，当总时长超过输入的timeout时，会直接熔断该循环，并直接退出程序，剩余的任务全部标记为timeout
最终输出总时长，并将每个任务的名称，状态，尝试次数，持续时长，开始时间，结束时间一并打印成输入的日志文件json里面

python3 scheduler.py --config tasks_demo.yaml（这个是输入的文件名） \--timeout 15 （这个是timeout时长，是数字）--report report.json（这个是输出的日志文件名） --seed 40（这个是随机数，方便重复验证，是数字）
    
    
 实例1（最后一个任务超时，直接退出）   python3 scheduler.py --config tasks_demo.yaml \--timeout 15 --report report.json --seed 420
 
 实例2（有Navigate，Grasp任务失败重试一次，结果未超时）    python3 scheduler.py --config tasks_demo.yaml \--timeout 15 --report report.json --seed 40
 
 实例3（全部一次成功）    python3 scheduler.py --config tasks_demo.yaml \--timeout 15 --report report.json --seed 4
 
 实例4（这期神了之DetectQR失败，然后Grasp跳过，然后AvoidObstacle失败两次超时，Place和RetuenHome随之超时）   python3 scheduler.py --config tasks_demo.yaml \--timeout 15 --report report.json --seed 521