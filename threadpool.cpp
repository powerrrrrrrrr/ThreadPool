#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THREADHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 60;//单位：秒

ThreadPool::ThreadPool() : initThreadSize_(4)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, taskQueMaxThreadHold_(TASK_MAX_THREADHOLD)
	, poolMode_(PoolMode::MODE_FIXED) 
	, isPoolRuning_(false)
{}

ThreadPool::~ThreadPool(){
	isPoolRuning_ = false;
	notEmpty_.notify_all();
	//等待线程池里面所有的线程返回
	//有两种状态： 阻塞 && 正在执行中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0;});
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode) {
	if (checkRuningState()) {
		return;
	}
	poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreadHold(int threadhold) {
	taskQueMaxThreadHold_ = threadhold;
}

//设置线程池cached模式下线程数量上限阈值
void ThreadPool::setThreadSizeThreshHold(int threadSizeThreshHold) {
	if (checkRuningState()) {
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threadSizeThreshHold;
	}
}

//给线程池提交任务   用户调用该接口，传入任务对象
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//线程的通信 等待任务队列有空余
	//用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreadHold_;})) {

		std::cerr << "task queue is full, submit task fail." << std::endl;
		//return task->getResult();
		return Result(sp, false);
	}
	//如果有空余，把任务放入任务队列
	taskQue_.emplace(sp);
	taskSize_++;

	//notEmpty_通知
	notEmpty_.notify_all();

	//cached模式，任务处理比较紧急
	//需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_) {
		//创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,
			this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
		threads_[threadId]->start();
		curThreadSize_++;
		idleThreadSize_++;
	}


	//返回任务的Result对象
	//return task->getResult()
	return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize) {
	//记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	isPoolRuning_ = true;
	//创建线程对象
	for (int i = 0; i < initThreadSize_; ++i) {
		//创建Thread线程对象的时候，把线程函数给到thread线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, 
			this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
	}
	//启动所有线程
	for (int i = 0; i < initThreadSize_; ++i) {
		threads_[i]->start();
		idleThreadSize_++;
	}
}

//定义线程函数  线程池的所有线程从任务队列里面消费任务
void ThreadPool::threadFunc(int threadid) {
	//std::cout << "begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	//std::cout << "end threadFunc tid:" << std::this_thread::get_id() << std::endl;
	auto lastTime = std::chrono::high_resolution_clock().now();
	
	while (isPoolRuning_) {

		std::shared_ptr<Task> task;
		{
			//获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id
				<< "尝试获取任务..." << std::endl;

			//在cached模式下，有可能已经创建了很多的线程
			//但是空闲时间超过60s，应该把多余的线程结束回收掉
			//当前时间 - 上一次线程执行时间 》 60s
			while (taskQue_.size() == 0) {
				if (poolMode_ == PoolMode::MODE_CACHED) {
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//开始回收线程
							//记录线程相关变量的值修改
							//把线程对象从线程列表容器中删除
							//threadId -> 线程 -> 删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id()
								<< "exit!" << std::endl;
							return;
						}
					}
				}
				else {
					//等待notEmpty_条件
					notEmpty_.wait(lock);
				}
				//线程池要结束，回收线程资源
				if (!isPoolRuning_) {
					threads_.erase(threadid);

					std::cout << "threadid:" << std::this_thread::get_id()
						<< "exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "获取任务成功" << std::endl;

			//不空，从任务队列中取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			
			//如果依然有剩余任务，继续通知其他线程执行任务
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			//取出一个任务，进行通知,通知可以继续提交生产任务
			notFull_.notify_all();
		}//释放锁

		//当前线程负责执行这个任务
		if (task != nullptr) {
			//task->run();
			task->exec();
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//更新线程执行玩任务的时间
	}


	threads_.erase(threadid);
	
	std::cout << "threadid:" << std::this_thread::get_id()
		<< "exit!" << std::endl;
	exitCond_.notify_all();
}

//检查pool的运行状态
bool ThreadPool::checkRuningState() const{
	return isPoolRuning_;
}

/// <summary>
/// 线程方法实现
/// </summary>

int Thread::generateId_ = 0;

//线程构造
Thread::Thread(ThreadFunc func) : func_(func), threadId_(generateId_++){}
//线程析构
Thread::~Thread() {}

//启动线程
void Thread::start() {
	//创建一个线程
	std::thread t(func_, threadId_);
	t.detach();//设置分离线程
}

//获取线程id
int Thread::getId() const {
	return threadId_;
}

/// <summary>
/// Task方法实现
/// </summary>
Task::Task() : result_(nullptr)
{}

void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run());
	}
}

void Task::setResult(Result* res) {
	result_ = res;
}

/// <summary>
/// Result方法的实现
/// </summary>
/// <param name="task"></param>
/// <param name="isValid"></param>
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid), task_(task)
{
	task_->setResult(this);
	 
}

Any Result::get() {
	if (!isValid_) {
		return "";
	}
	sem_.wait();
	return std::move(any_);
}

void Result::setVal(Any any) {
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();
}