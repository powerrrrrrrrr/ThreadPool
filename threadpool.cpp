#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THREADHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 60;//��λ����

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
	//�ȴ��̳߳��������е��̷߳���
	//������״̬�� ���� && ����ִ����
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0;});
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode) {
	if (checkRuningState()) {
		return;
	}
	poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreadHold(int threadhold) {
	taskQueMaxThreadHold_ = threadhold;
}

//�����̳߳�cachedģʽ���߳�����������ֵ
void ThreadPool::setThreadSizeThreshHold(int threadSizeThreshHold) {
	if (checkRuningState()) {
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threadSizeThreshHold;
	}
}

//���̳߳��ύ����   �û����øýӿڣ������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�̵߳�ͨ�� �ȴ���������п���
	//�û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreadHold_;})) {

		std::cerr << "task queue is full, submit task fail." << std::endl;
		//return task->getResult();
		return Result(sp, false);
	}
	//����п��࣬����������������
	taskQue_.emplace(sp);
	taskSize_++;

	//notEmpty_֪ͨ
	notEmpty_.notify_all();

	//cachedģʽ��������ȽϽ���
	//��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_) {
		//�������߳�
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,
			this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
		threads_[threadId]->start();
		curThreadSize_++;
		idleThreadSize_++;
	}


	//���������Result����
	//return task->getResult()
	return Result(sp);
}

//�����̳߳�
void ThreadPool::start(int initThreadSize) {
	//��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	isPoolRuning_ = true;
	//�����̶߳���
	for (int i = 0; i < initThreadSize_; ++i) {
		//����Thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, 
			this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
	}
	//���������߳�
	for (int i = 0; i < initThreadSize_; ++i) {
		threads_[i]->start();
		idleThreadSize_++;
	}
}

//�����̺߳���  �̳߳ص������̴߳��������������������
void ThreadPool::threadFunc(int threadid) {
	//std::cout << "begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	//std::cout << "end threadFunc tid:" << std::this_thread::get_id() << std::endl;
	auto lastTime = std::chrono::high_resolution_clock().now();
	
	while (isPoolRuning_) {

		std::shared_ptr<Task> task;
		{
			//��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id
				<< "���Ի�ȡ����..." << std::endl;

			//��cachedģʽ�£��п����Ѿ������˺ܶ���߳�
			//���ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳̽������յ�
			//��ǰʱ�� - ��һ���߳�ִ��ʱ�� �� 60s
			while (taskQue_.size() == 0) {
				if (poolMode_ == PoolMode::MODE_CACHED) {
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//��ʼ�����߳�
							//��¼�߳���ر�����ֵ�޸�
							//���̶߳�����߳��б�������ɾ��
							//threadId -> �߳� -> ɾ��
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
					//�ȴ�notEmpty_����
					notEmpty_.wait(lock);
				}
				//�̳߳�Ҫ�����������߳���Դ
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
				<< "��ȡ����ɹ�" << std::endl;

			//���գ������������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			
			//�����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			//ȡ��һ�����񣬽���֪ͨ,֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		}//�ͷ���

		//��ǰ�̸߳���ִ���������
		if (task != nullptr) {
			//task->run();
			task->exec();
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//�����߳�ִ���������ʱ��
	}


	threads_.erase(threadid);
	
	std::cout << "threadid:" << std::this_thread::get_id()
		<< "exit!" << std::endl;
	exitCond_.notify_all();
}

//���pool������״̬
bool ThreadPool::checkRuningState() const{
	return isPoolRuning_;
}

/// <summary>
/// �̷߳���ʵ��
/// </summary>

int Thread::generateId_ = 0;

//�̹߳���
Thread::Thread(ThreadFunc func) : func_(func), threadId_(generateId_++){}
//�߳�����
Thread::~Thread() {}

//�����߳�
void Thread::start() {
	//����һ���߳�
	std::thread t(func_, threadId_);
	t.detach();//���÷����߳�
}

//��ȡ�߳�id
int Thread::getId() const {
	return threadId_;
}

/// <summary>
/// Task����ʵ��
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
/// Result������ʵ��
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
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();
}