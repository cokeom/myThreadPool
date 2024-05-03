#include <iostream>
#include "threadpool.h"
#include <chrono>
#include <thread>
#include <string>

using uLong = unsigned long long;

// 有的时候，我们希望获得线程的处理返回值
// 如何获取返回值？如何获取各种类型的返回值，因为纯虚函数的存在，无法用模板类型
class MyTask : public Task {
public:
    MyTask(uLong begin, uLong end) : begin_(begin), end_(end){};

    Any run() {
        std::cout << "tid :" << std::this_thread::get_id() << " begin!" << std::endl;
        uLong sum = 0;
//        std::this_thread::sleep_for(std::chrono::seconds(3));
        for (uLong i = begin_; i <= end_; i++) {
            sum += i;
        }
        std::cout << "tid :" << std::this_thread::get_id() << " end!" << std::endl;
        return sum;
    }
private:
    uLong begin_;
    uLong end_;
};

int main() {
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.start(3);
        Result res1 = pool.submitTask(std::make_shared<MyTask>(0, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

//        Result res4 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
//        Result res5 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
//        Result res6 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(100, 300));
//    pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

//    std::this_thread::sleep_for(std::chrono::seconds(10));
        uLong sum1 = res1.get().cast_<uLong>();
        uLong sum2 = res2.get().cast_<uLong>();
        uLong sum3 = res3.get().cast_<uLong>();
        // Master - Slave线程模型，Master分解任务，Slave执行任务，等待各个Slave线程执行完
        // Master 合并任务结果
        std::cout << sum1 + sum2 + sum3 << std::endl;
    }
    getchar();

    return 0;
}
