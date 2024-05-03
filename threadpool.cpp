//
// Created by Coke Lin on 2024/5/2.
//
#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024; // 可以设置为INT32_MAX
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10; // 线程空闲上限时间,单位是s，仅针对cached模式

// *************线程池方法实现******************
ThreadPool::ThreadPool() : initThreadSize_(0),
                           taskSize_(),
                           taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD),
                           pollMode_(PoolMode::MODE_FIXED),
                           isPoolRunning_(false),
                           idleThreadSize_(0),
                           threadMaxThreshHold_(THREAD_MAX_THRESHHOLD),
                           currThreadSize_(0)
                           {};

// 这是一种态度
ThreadPool::~ThreadPool() {
    isPoolRunning_ = false;

    notEmpty_.notify_all(); // 从等待到阻塞状态

    // 等待所有线程返回
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    exitCond_.wait(lock, [&]() {return threads_.size() == 0;});
}

void ThreadPool::start(int initThreadSize) {
    // 设置线程池的启动状态，防止Set操作
    isPoolRunning_ = true;
    // 初始线程池大小
    initThreadSize_ = initThreadSize;
    currThreadSize_ = initThreadSize;

    for (int i = 0; i < initThreadSize_; i++) {
        auto ptr = std::make_unique<Thread>
                (std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
//        threads_.emplace_back(std::move(ptr));
    }
    for (int i = 0; i < initThreadSize_; i++) {
        threads_[i]->start();
        idleThreadSize_++; // 记录空闲线程个数，因为此时还没有执行task，只是执行线程函数去找task
    }

}

void ThreadPool::setMode(PoolMode mode) {
    if (checkRunningState()) return;
    pollMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
    if (checkRunningState() || pollMode_ == PoolMode::MODE_FIXED) return;
    taskQueMaxThreshHold_ = threshhold;
}

// 用户添加任务，增加任务队列，生产者
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    // 后面lambda表达式是条件变量的条件 wait wait_for wait_until
    //    notFull_.wait(lock, [&](){return taskSize_ < taskQueMaxThreshHold_;});

    if (!notFull_.wait_for(lock, std::chrono::seconds(1),
                           [&](){return taskSize_ < taskQueMaxThreshHold_;} )){
        // 如果等来false，表示等待时间超过了1s
        std::cerr << "任务队列满了，添加任务失败！" << std::endl;
//        return task->getResult(); // 不好，如果Task生命周期没了，就无法获得task
        return Result(sp, false);
    }

    // 如果有空余则放入任务队列
    myTaskQue_.emplace(sp);
    taskSize_++;

    // 因为放了任务，任务队列肯定不空，在not_empty上通知
    notEmpty_.notify_all(); // 分配线程执行任务

    // cached模式 需要根据任务和空闲线程的数量，判断是否要增大线程个数（任务比较紧急，小而快的任务，否则久久占用系统资源）
    if (pollMode_ == PoolMode::MODE_CACHED
        && taskSize_ > idleThreadSize_
        && currThreadSize_ < threadMaxThreshHold_) {
        // 创建新线程
        std::cout << " [CACHED] 创建新线程!" << std::endl;
        auto ptr = std::make_unique<Thread>
                (std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        // 启动线程
        threads_[threadId]->start();
        // 修改相关变量
        idleThreadSize_++;
        currThreadSize_++;
    }

    // 返回任务的Result对象
    return Result(sp);

}

// 线程池的所有线程执行该函数，从任务队列中获取任务执行，消费任务队列
void ThreadPool::threadFunc(int threadId) {
    auto lastTime = std::chrono::high_resolution_clock().now();

    while (isPoolRunning_) {
        std::shared_ptr<Task> task = nullptr;
        // 先获取锁
        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            std::cout << "尝试获取任务" << std::endl;

            while (myTaskQue_.size() == 0) {
                // cached模式下，可能创建了很多线程
                // 但是空闲时间超过60s的线程应该被结束回收掉(超过initThreadSize_的线程)
                // 当前时间 - 上一次执行时间 > 60s
                if (pollMode_ == PoolMode::MODE_CACHED) {
                    // 每1s返回一次 需要判断是超时返回，还是任务待执行返回
                    if (notEmpty_.wait_for(lock, std::chrono::seconds(1))
                        == std::cv_status::timeout) {
                        // 超时返回了，计算时间差值
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur =
                                std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME
                            && currThreadSize_ > initThreadSize_) {
                            // 回收当前线程
                            // 1. 修改记录线程数量相关的值
                            // 2. 把线程对象从线程列表容器中删除
                            // (难点：threadFunc怎么匹配线程对象？将threads_修改为哈希映射表)
                            // 应该从线程id -> 线程 -> 删除对应线程
                            threads_.erase(threadId);
                            currThreadSize_--;
                            idleThreadSize_--;

                            std::cout << "id :" << std::this_thread::get_id() << " exit!" << std::endl;
                            return;
                        }
                    }
                } else { // FIXED模式
                    // 等待notEmpty条件，FIXED下随便等
                    notEmpty_.wait(lock);
                }

                // 如果是因为线程池结束了，收到了信号，则直接推出
                if (!isPoolRunning_) {
                    threads_.erase(threadId);
                    std::cout << "id :" << std::this_thread::get_id() << " exit!" << std::endl;
                    exitCond_.notify_all();
                    return;
                }
            }

            // 空闲线程数量减去1
            idleThreadSize_--;

            // 从任务队列取一个任务
            task = myTaskQue_.front(); myTaskQue_.pop();
            taskSize_--;

            // 如果还有任务，通知一下
            if (myTaskQue_.size() > 0) {
                notEmpty_.notify_all();
            }

            // 取出一个任务，可以通知生产任务
            notFull_.notify_all();
        }   // 这里就应该释放锁了，

        std::cout << "获取任务成功" << std::endl;
        // 执行
        if (task != nullptr) {
            std::cout << "执行任务" << std::endl;
            task->exec(); // 需要将run的返回值保存到Result，加一层封装
        } else {
            std::cerr << "任务为空，出错" << std::endl;
        }

        // 处理完任务了，空间线程加1
        idleThreadSize_++;

        // 更新线程执行完任务的时间
        lastTime = std::chrono::high_resolution_clock().now();
    }
    // 执行完任务，发现线程池结束了
    threads_.erase(threadId); // 删除对应线程
    std::cout << "id :" << std::this_thread::get_id() << " exit!" << std::endl;
    exitCond_.notify_all();
}

bool ThreadPool::checkRunningState() const {
    return isPoolRunning_;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold) {
    if (checkRunningState()) return;
    threadMaxThreshHold_ = threshhold;
};

// *********线程方法实现*************

Thread::Thread(ThreadFunc func) :
        func_(func),
        threadId_(generateId_++)
        {}

Thread::~Thread() {

}

void Thread::start() {
    // 创建一个线程执行线程函数
    std::thread t(func_, threadId_); // C++11 线程对象和线程函数
    t.detach(); // 线程对象和线程函数分离，只析构线程对象 ???????
}

int Thread::generateId_ = 0;

int Thread::getId() const {
    return threadId_;
}


// ********Task方法实现**************
Task::Task() : result_(nullptr) {};

void Task::exec() {
    if (result_ != nullptr) { // 如果需要返回值
        result_->setVal(run()); // 多态调用
    } else { // 如果不需要返回值
        run();
    }
}

void Task::setResult(Result *result) {
    result_ = result;
}



// ********Result方法实现************
Result::Result(std::shared_ptr<Task> task, bool isValid) : task_(task), isValid_(isValid){
    task_->setResult(this);
}
Result::~Result() {
    task_->setResult(nullptr);
}

Any Result::get() {
    if (!isValid_) {
        return "";
    }
//    return any_; // 拷贝构造，不允许
    sem_.wait(); // task任务如果没有执行完，则阻塞与此
    return std::move(any_);
}

void Result::setVal(Any any) {
    // 存储task返回值
    this->any_ = std::move(any);
    sem_.post(); // 已经获取到任务的返回值，增加信号量资源
};