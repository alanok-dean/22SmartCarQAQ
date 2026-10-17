import yaml,json
import sys
import time,random
import argparse
parser = argparse.ArgumentParser(description="M0-4 TaskScheduler")#导入数据
parser.add_argument("--config",type=str,required=True,help="配置文件路径")
parser.add_argument("--timeout",type=float,help="超时时间")
parser.add_argument("--report",type=str,default="report.json",help="报告输出路径")
parser.add_argument("--seed",type=int,help="随机种子")
args = parser.parse_args()

if sys.stdout.isatty():#处理颜色
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    BLUE =  "\033[94m"
    RESET = "\033[0m"
else:
    GREEN = RED =YELLOW = BLUE = RESET = ""

if args.seed is not None:#锁定随机数
    random.seed(args.seed)

try :#录入yaml或json文件
    file_path = args.config
    if file_path.endswith(".yaml"):
        with open (args.config,"r") as f:
            config = yaml.safe_load(f)
            print(f"成功读取yaml文件:{file_path}")
    
    elif file_path.endswith(".json"):
        with open (args.config,"r") as f:
            config = json.load(f)
            print(f"成功读取json文件:{file_path}")
    else :
        print("输入文件错误,请重试")
        sys.exit(1)
    
    task_list = config["tasks"]
    if args.timeout is not None: #无输入随机数则固定使用文件中随机数
        timeout = args.timeout
    else:
        timeout = config["timeout"]

except Exception as a:
    print("读取文件错误,请重试",a)
    sys.exit(1) 

class Task : #定义任务类
    def __init__(self,name,duration,success_rate,dependencies):
        self.name = name
        self.duration = duration
        self.success_rate = success_rate
        self.dependencies = dependencies

        self.status = "PENDING"
        self.attempts = 0
        self.started_at = None
        self.ended_at = None

tasks_dict = {}  #把所有任务封装到词典里，为下面使用准备
for task in task_list :
    name = task["name"]
    duration = task["duration"]
    success_rate = task["success_rate"] 
    dependencies = task["dependencies"]
    new_task = Task(name,duration,success_rate,dependencies)
    tasks_dict[name] = new_task

class TaskScheduler : #定义任务总管理类
    def __init__(self,tasks_dict,timeout):
        self.tasks = tasks_dict
        self.timeout = timeout
        self.total_duration = 0.0
        self.is_timeout = False
    
    def task_run(self):  #定义任务运行方法
        print("--现在开始调度任务--")
        start_time = time.time()#记录总开始时间
        print()
        
        while True :   #1用列表记录任务处理状态，有则继续循环，无则终止
            pending_tasks = []
            for t in self.tasks.values():
                if t.status == "PENDING":
                    pending_tasks.append(t)
            if not pending_tasks:
                print("--!!!所有任务处理完成!!!--")
                break
            
            if time.time() - start_time > timeout : #2超时终止
                self.is_timeout = True
                print("TIMEOUT")
                print("--已超时，调度停止--")
                for t in pending_tasks :
                    t.status = "TIMEOUT"
                break

            check_this_round = False  #3检查是否任务未开始
            for name,task in self.tasks.items() :
                if task.status != "PENDING":
                    continue
                
                depend_ready  = True    #4检查前置依赖环节
                depend_failed = False
                for depend_name in task.dependencies:
                    depend_task = self.tasks[depend_name]
                    if depend_task.status in ["FAILED","SKIPPED","TIMEOUT"]:
                        depend_failed = True
                        break
                    elif depend_task.status != "SUCCESS":
                        depend_ready =False
                        break

                if depend_failed:   #4-1前置任务失败，当前任务标记为SKIPPED
                    task.status = "SKIPPED"
                    print (f"{BLUE}--任务{name}因前置任务失败而被SKIPPED  SKIPPED--{RESET}")
                    print()
                    continue

                if not depend_ready:  #4-2前置任务尚未开始，跳过当前任务
                    continue

                check_this_round = True  #5前置成功，开始执行任务
                print(f"--正在执行任务{name}--")
                print()
                success = False   
                while task.attempts < 3:    #循环执行任务
                    task.attempts += 1
                    task.started_at = time.time()
                    if task.attempts >1:
                        print(f"{YELLOW}--任务{name}重试中  RETRY--{RESET}")
                        print()

                    time.sleep(task.duration)

                    if random.random() < task.success_rate:
                        task.status = "SUCCESS"
                        print(f"{GREEN}--任务{name}执行成功  SUCCESS--{RESET}")
                        print()
                        print()
                        success = True
                        break
                    else :
                        task.status = "FAILED"
                        print(f"--{RED}任务第{task.attempts}次{name}执行失败  FAILED--{RESET}")
                        print()
                    task.ended_at = time.time()


                if not success:
                    print(f"{RED}--任务{name}彻底失败  FAILED--{RESET}")
                    print()
                    print()
                    task.status = "FAILED"
                task.ended_at = time.time()


                if time.time() - start_time > timeout : #2超时终止
                    self.is_timeout = True
                    print()
                    print("TIMEOUT")
                    print("--已超时，调度停止--")
                    for t in self.tasks.values() :
                        if t.status == "PENDING":
                            t.status = "TIMEOUT"
                    break

            if not check_this_round and not self.is_timeout:
                print("存在循环依赖，无法继续运行")
                break

            self.total_duration += time.time() - start_time
            print (f"目前总时长为：",self.total_duration,"秒")
            print()
                            
    def generate_report(self,report_path):
        report_data = {
                        "timeout": self.is_timeout,
                        "total_duration": round(self.total_duration,2),
                        "tasks": []
                      }
        for task in self.tasks.values():
            report_data["tasks"].append({
                "name":task.name,
                "status":task.status,
                "attempts":task.attempts,
                "duration":task.duration,
                "started_at":task.started_at,
                "ended_at":task.ended_at,
            })

        with open(report_path,"w") as f:
            json.dump(report_data,f,indent=2,ensure_ascii=False)
            print(f"json报告已成功生成并保存到:{report_path}")



scheduler = TaskScheduler(tasks_dict,timeout)
scheduler.task_run()
scheduler.generate_report("report.json")