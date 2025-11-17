#include "ThreadPool.h"

#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) : num_threads_(num_threads)
{
    stop_ = false;
    // 根据线程数初始化分片资源
    // 1. 初始化分片队列、互斥锁和条件变量
    for (size_t i = 0; i < num_threads_; ++i)
    {
        // task_queues_ (std::queue)
        sharded_queues_.emplace_back();

        // queue_mutexes_ (std::mutex) - 必须使用 emplace_back
        queue_mutexes_.push_back(std::make_unique<std::mutex>());

        // conditions_ (std::condition_variable) - 必须使用 emplace_back
        conditions_.push_back(std::make_unique<std::condition_variable>());
    }

    for (size_t i = 0; i < num_threads_; ++i)
    {
        // 创建并启动线程。将线程与特定的队列索引 i 绑定。
        workers_.emplace_back(&ThreadPool::worker_loop, this, i); // 就地构造一个thread 传入回调函数以及相关参数
    }
    std::cout << "ThreadPool initialized with " << num_threads_ << " worker threads (Affinity Mode)." << std::endl;
}
// 析构函数
ThreadPool::~ThreadPool()
{
    stop_ = true;

    // 唤起所有变量
    for (auto &cond : conditions_)
    {
        cond->notify_all();
    }

    // 等待所有线程执行完任务
    for (auto &worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}
void ThreadPool::worker_loop(size_t queue_index)
{
    // 线程获取自己专属的资源引用，避免每次查找
    // 注意：task_queues_ 是直接存储 std::queue，所以是引用
    std::queue<AffinityTask> &my_queue = sharded_queues_[queue_index];

    // 注意：mutex 和 condition_variable 是 unique_ptr，所以是 unique_ptr 引用
    std::unique_ptr<std::mutex> &my_mutex = queue_mutexes_[queue_index];
    std::unique_ptr<std::condition_variable> &my_condition = conditions_[queue_index];

    for (;;)
    {
        AffinityTask task;

        { // 临界区：获取任务
          // 使用 *my_mutex 解引用 unique_ptr 获取实际的 mutex 对象
            std::unique_lock<std::mutex> lock(*my_mutex);

            // 使用条件变量等待：线程在此挂起，直到被通知 (notify) 且满足条件
            my_condition->wait(lock, [this, &my_queue]
                              {
                // 条件：线程池停止 或 自己的队列非空
                return this->stop_.load() || !my_queue.empty(); }); // 如果线程池停止或者队列不为空，直接返回执行下面代码 否则（线程池未停止、任务队列为空）挂起等待；

            // 优雅退出：如果线程池停止，且队列中已无任务，则退出循环
            if (this->stop_ && my_queue.empty())
                return;

            // 从队列头部取出任务
            task = std::move(my_queue.front()); // 移动赋值，避免拷贝造成的性能下降；
            my_queue.pop();
        } // 锁自动释放

        // 关键：在锁外执行任务，避免阻塞队列，影响其他线程的投递操作
        try
        {
            task.func();
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Worker " << queue_index << "] Caught exception: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "[Worker " << queue_index << "] Caught unknown exception." << std::endl;
        }
    }
}
