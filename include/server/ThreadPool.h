#pragma once
#include <mutex>
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
// 定义用于分组的任务结构体
struct AffinityTask
{
    long long key;              // 用于分组的键（如 sessionid）
    std::function<void()> func; // 要执行的实际函数
};

class ThreadPool
{
public:
    ThreadPool(size_t num_threads);
    ~ThreadPool();

    template <typename F>
    void enqueue_with_key(long long key, F &&f) // 把任务加入到相应的任务队列
    {
        // 计算位于哪个index
        // 确保同一 Key 的任务总是被映射到同一个索引（同一个队列）
        size_t index = std::hash<long long>{}(key) % num_threads_;

        // 2. 构造任务
        AffinityTask affinity_task = {key, std::forward<F>(f)};
        {
            // 获取对应队列锁
            std::unique_lock<std::mutex> lock(*queue_mutexes_[index]);

            if (stop_)
            {
                throw std::runtime_error("Attempted to enqueue task on a stopped ThreadPool.");
            }
            sharded_queues_[index].push(std::move(affinity_task));
        }

        conditions_[index]->notify_one();
    }
    template <typename F>
    void enqueue(F &&f)
    {
        // 1. 使用轮询策略选择一个队列索引
        // next_queue_index_++ 确保原子递增，% num_threads_ 保证在有效范围内
        size_t index = next_queue_index_++ % num_threads_;

        // 2. 构造一个 AffinityTask，Key 可以设置为 0 或其他无效值
        AffinityTask general_task = {0, std::forward<F>(f)};

        {
            // 获取对应队列锁
            std::unique_lock<std::mutex> lock(*queue_mutexes_[index]);

            if (stop_)
            {
                throw std::runtime_error("Attempted to enqueue task on a stopped ThreadPool.");
            }

            // 投递到选定的分片队列
            sharded_queues_[index].push(std::move(general_task));
        }

        // 通知等待的线程
        conditions_[index]->notify_one();
    }

private:
    // 记录线程数量
    size_t num_threads_;
    // 储存工作线程
    std::vector<std::thread> workers_;
    // 任务队列 为了保证玩家操作的执行顺序，采用切片队列，让相同玩家的操作进入同一个队列
    // 关键：分片队列数组。每个工作线程独享一个队列。
    std::vector<std::queue<AffinityTask>> sharded_queues_;
    // 线程安全访问切片任务队列锁
    // 关键：互斥锁数组。每个队列都有自己的互斥锁，实现细粒度锁定，提高并发度。
    std::vector<std::unique_ptr<std::mutex>> queue_mutexes_; // 队列互斥锁分片
    // 关键：条件变量数组。每个队列有自己的条件变量，用于通知相应的线程。
    std::vector<std::unique_ptr<std::condition_variable>> conditions_; // 条件变量分片
    // 工作线程队列
    std::vector<std::thread> threads_;
    // 线程池停止标志
    std::atomic<bool> stop_ = false;
    // 工作线程循环，每个线程负责一个队列
    void worker_loop(size_t queue_index);
    std::atomic<size_t> next_queue_index_ = 0; // 用于轮询分发通用任务
};
