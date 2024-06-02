#include <iostream>
#include <chrono>
#include <thread>
using namespace std;

#include "threadpool.h"

using uLong = unsigned long long;

class MyTask : public Task {
public:
	MyTask(int begin, int end) : begin_(begin), end_(end) {

	}
	//问题一：怎么设计run函数的返回值，可以表示任意的类型 
	Any run() {
		std::cout << "tid:" << std::this_thread::get_id()
			<< "begin!" << std::endl;
		
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; ++i) {
			sum += i;
		}

		std::cout << "tid:" << std::this_thread::get_id()
			<< "end!" << std::endl;
		return sum;
	}

private:
	int begin_;
	int end_;
};

int main() {
	//问题：ThreadPool对象析构以后，怎么样把线程池相关的线程资源全部回收
	{
		ThreadPool pool;
		//用户自己设置工作模式
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(4);

		//问题二：如何设计这里的Result机制
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1000001, 2000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));

		uLong sum1 = res1.get().cast_<uLong>();//get返回了一个Any类型，怎么转成具体的类型
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		//Master - Slave 线程模型
		//Master线程用来分解任务，然后给各个Slave线程分配任务
		cout << (sum1 + sum2 + sum3) << endl;
	}

	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());


	getchar();
}