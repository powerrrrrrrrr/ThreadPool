#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>


//Any���ͣ����Խ����������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//������캯��������Any���ͽ�����������������
	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)){}

	//��������ܰ�Any��������洢��data������ȡ����
	template<typename T>
	T cast_() {
		//������ô��base_�ҵ�����ָ���Derive���󣬴���ȡ��data��Ա����
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) {
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//��������
	class Base {
	public:
		virtual ~Base() = default;
	};

	//����������
	template<typename T>
	class Derive : public Base {
	public:
		Derive(T data) : data_(data){}
		T data_;//�������������������
	};

private:
	//����һ������ָ��
	std::unique_ptr<Base> base_;
};

//ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit = 0) : resLimit_(limit) {}
	~Semaphore() = default;

	void wait() {
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]()->bool{return resLimit_ > 0;});
		resLimit_--;
	}
	void post() {
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	std::mutex mtx_;
	std::condition_variable cond_;
	int resLimit_;
};

class Result;
//����������
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	//�û������Զ��������������ͣ���Task�̳У���дrun����
	//ʵ���Զ���������
	virtual Any run() = 0;
private:
	Result* result_;
};

//ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//����һ��setVal��������ȡ����ִ����ķ���ֵ
	void setVal(Any any);

	//�������get�������û��������������ȡtask�ķ���ֵ
	Any get();

private:
	Any any_;//�洢����ķ���ֵ
	Semaphore sem_;//�߳�ͨ���ź���
	std::shared_ptr<Task> task_;//ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_;
};

//�̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	MODE_FIXED,//�̶��߳�����ģʽ
	MODE_CACHED,//�ɱ��߳�����ģʽ
};


/// <summary>
/// �߳�����
/// </summary>
class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;
	//�̹߳���
	Thread(ThreadFunc func);
	//�߳�����
	~Thread();

	//�����߳�
	void start();

	//��ȡ�߳�id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //�����߳�id
};

/// <summary>
/// �̳߳�����
/// </summary>
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	//����task�������������ֵ
	void setTaskQueMaxThreadHold(int threadhold);

	//�����̳߳�cachedģʽ���߳�����������ֵ
	void setThreadSizeThreshHold(int threadSizeThreshHold);

	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	//�����̳߳�
	void start(int initThreadSize = 4);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator = (const ThreadPool&) = delete;
private:
	//�����̺߳���
	void threadFunc(int threadid);

	//���pool������״̬
	bool checkRuningState() const;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;//�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�
	int initThreadSize_;//��ʼ�߳�����
	int threadSizeThreshHold_;//�߳�����������ֵ
	std::atomic_int curThreadSize_; //��¼��ǰ�̳߳������̵߳�������
	std::atomic_int idleThreadSize_;//�����߳�����

	std::queue<std::shared_ptr<Task>> taskQue_;//�������
	std::atomic_int taskSize_;//��������
	int taskQueMaxThreadHold_;//�����������������ֵ

	std::mutex taskQueMtx_;//��֤������е��̰߳�ȫ
	std::condition_variable notFull_;//��ʾ������в���
	std::condition_variable notEmpty_;//��ʾ������в���
	std::condition_variable exitCond_;

	PoolMode poolMode_;//��ǰ�̳߳ع���״̬
	std::atomic_bool isPoolRuning_;//��ʾ��ǰ�̳߳ص�����״̬
};


#endif // !THREADPOOL_H