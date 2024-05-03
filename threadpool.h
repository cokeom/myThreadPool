//
// Created by Coke Lin on 2024/5/2.
//

#ifndef MYTHREADPOOL_THREADPOOL_H
#define MYTHREADPOOL_THREADPOOL_H

#include <vector>
#include <thread>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

enum class PoolMode { // 线程池支持的模式
    MODE_FIXED,
    MODE_CACHED,
};

// Any类型，接收所有类型的变量
class Any {
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    // 这个构造函数能使得Any接收所有类型的数据
    template<typename T>
    Any(T data) : ptr_(std::make_unique<Derive<T>>(data)) {};

    template<typename T>
    T cast_() {
        auto derivePtr = dynamic_cast<Derive<T>*>(ptr_.get());
        if (derivePtr == nullptr) {
            throw "类型不匹配";
        }
        return derivePtr->data;
    }

private:
    // 基类
    class Base {
    public:
        virtual ~Base() = default;
    };
    // 派生类 + 模板类型
    template <typename T>
    class Derive : public Base {
    public:
        Derive(T data) : data(data){};
        ~Derive() override {};
//    private: // 不能将data声明为private，会导致强转指针后无法访问数据
        T data;
    };

private:
    std::unique_ptr<Base> ptr_;
};

// 实现一个简单的信号量，用于线程通信
class Semaphore {
public:
    Semaphore (int limit = 0) : resLimit_(limit){};
    ~Semaphore(){};

    // 获取一个信号量资源
    void wait() {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]() {return resLimit_ > 0;});
        resLimit_--;
    }

    // 增加一个信号量资源
    void post() {
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

class Task; // Task类型前置声明

// 实现 接收提交到线程池的task 执行完成后的返回类型 result
class Result {
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result();

    // 问题一：setVal获取任务返回值any_
    void setVal(Any any);

    // 问题二：get方法获取返回值，如果还没有就阻塞，简单的同步问题
    Any get();
private:
    Any any_; // 存储任务的返回值
    Semaphore sem_; // 线程通信的信号量
    std::shared_ptr<Task> task_; // 指向获取返回值的任务对象
    std::atomic_bool isValid_; // 返回值是否有效
};

// 任务抽象基类，用来接收用户传入的不同任务，多态，从TASK继承
class Task {
public:
    Task();
    ~Task() = default;
    void exec();
    void setResult(Result* result);
    virtual Any run() = 0;
private:
    Result* result_; // 注意千万不能写强智能指针，否则会互相引用，永远不会释放
};

/*
 * example:
 * ThreadPool pool;
 * pool.start(4);
 *
 * class MyTask : public Task {
 * public:
 *      void run() {
 *          // 线程代码
 *      }
 * }
 *
 * pool.submitTask(std::make_shared<MyTask>());
 * */
class Thread {
public:
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func);
    ~Thread();
    // 启动线程
    void start();

    // 获取线程id
    int getId () const;
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; // 保存线程id，cached使用
};

class ThreadPool{
public:
    ThreadPool();
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void start(int initThreadSize = std::thread::hardware_concurrency());

    void setMode(PoolMode mode);

    void setTaskQueMaxThreshHold(int threshhold);

    void setThreadSizeThreshHold(int threshhold);

//    void setInitThreadSize(int size);

    Result submitTask(std::shared_ptr<Task> sp);

private:
    // 定义线程函数
    void threadFunc(int threadId);

    bool checkRunningState() const;

private:
//    std::vector<std::unique_ptr<Thread>> threads_; // 线程列表, 使用智能指针防止内存泄漏
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 兼容cached的线程列表，解决threadFunc找不到对应线程的问题
    size_t initThreadSize_; // 初始线程数量
    std::atomic_uint idleThreadSize_; // 记录空闲线程的数量
    std::atomic_uint currThreadSize_; // 记录当前线程池中线程数量
    int threadMaxThreshHold_; // 线程数量的上限（只有Cached模式下才使用）

    std::queue<std::shared_ptr<Task>> myTaskQue_; // 使用智能指针保证传入的Task生命周期，任务队列、
    std::atomic_uint taskSize_; // 任务数量

    int taskQueMaxThreshHold_; // 任务队列上限阈值
    std::mutex taskQueMtx_; // 保证任务队列线程安全
    std::condition_variable notFull_; // 表示任务队列不满
    std::condition_variable notEmpty_; // 表示任务队列不空
    std::condition_variable exitCond_;

    PoolMode pollMode_;
    std::atomic_bool isPoolRunning_; // 标识线程池是否运行了
};

#endif //MYTHREADPOOL_THREADPOOL_H
