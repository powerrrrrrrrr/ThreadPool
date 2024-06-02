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
	//����һ����ô���run�����ķ���ֵ�����Ա�ʾ��������� 
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
	//���⣺ThreadPool���������Ժ���ô�����̳߳���ص��߳���Դȫ������
	{
		ThreadPool pool;
		//�û��Լ����ù���ģʽ
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(4);

		//������������������Result����
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1000001, 2000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));

		uLong sum1 = res1.get().cast_<uLong>();//get������һ��Any���ͣ���ôת�ɾ��������
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		//Master - Slave �߳�ģ��
		//Master�߳������ֽ�����Ȼ�������Slave�̷߳�������
		cout << (sum1 + sum2 + sum3) << endl;
	}

	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());


	getchar();
}