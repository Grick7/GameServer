#include "GameServer.h"
#include <iostream>
#include "thread"
#include "ThreadPool.h"
#include "MessageDispatcher.h"
#include "PlayerDataManager.h" // 同步数据的类
#include "RoomManager.h"
#include <cppkafka/consumer.h> // Kafka 消费相关头文件
#include <cppkafka/configuration.h>
int main()
{
  try
  {
    boost::asio::io_context io;

    // 1️⃣ 创建工作线程池
    const size_t num_workers = std::thread::hardware_concurrency();
    ThreadPool worker_pool(num_workers);

    // 2️⃣ 消息分发器
    MessageDispatcher &dispatcher = MessageDispatcher::instance(worker_pool);

    // 4️⃣ 初始化 RoomManager 单例并传入 io_context
    RoomManager::getInstance(&io);

    // 3️⃣ 启动游戏服务器（监听端口）
    GameServer server(io, 8989, dispatcher,worker_pool);

    // 4️⃣ 启动 IO 线程池
    std::vector<std::thread> io_threads;
    for (int i = 0; i < 4; ++i)
    {
      io_threads.emplace_back([&io]()
                              { io.run(); });
    }

    std::cout << "[GameServer] Started successfully." << std::endl;

    // 5️⃣ 启动 Kafka 消费线程（异步同步 Redis → MySQL）
    std::thread kafka_thread([]()
                             {
            try
            {
                cppkafka::Configuration config = {
                    {"metadata.broker.list", "127.0.0.1:9092"},
                    {"group.id", "game_server_consumer"},
                    {"enable.auto.commit", true},
                    {"auto.offset.reset", "earliest"}  // 从头开始消费（调试时有用）
                };

                cppkafka::Consumer consumer(config);
                std::string topic = "playerdata_update";
                consumer.subscribe({topic});

                std::cout << "[KafkaConsumer] Started listening on topic: " << topic << std::endl;

                // 消费循环
                while (true)
                {
                    cppkafka::Message msg = consumer.poll();
                    if (!msg) continue; // 无消息则继续
                    if (msg.get_error())
                    {
                        if (!msg.is_eof())
                            std::cerr << "[KafkaError] " << msg.get_error() << std::endl;
                        continue;
                    }

                    // 解析消息内容
                    std::string payload = msg.get_payload();
                    std::istringstream iss(payload);
                    std::string uid_str, field, value_str;

                    if (std::getline(iss, uid_str, '|') &&
                        std::getline(iss, field, '|') &&
                        std::getline(iss, value_str))
                    {
                        int uid = std::stoi(uid_str);
                        int value = std::stoi(value_str);
                        std::cout << "[KafkaConsumer] Received message: " << payload << std::endl;

                        // 调用同步函数
                        PlayerDataManager::getInstance().syncToMySQL(uid, field, value);
                    }
                    else
                    {
                        std::cerr << "[KafkaConsumer] Invalid message format: " << payload << std::endl;
                    }
                }
            }
            catch (const std::exception &ex)
            {
                std::cerr << "[KafkaConsumer] Exception: " << ex.what() << std::endl;
            } });

    // 6️⃣ 等待所有线程退出
    for (auto &t : io_threads)
    {
      t.join();
    }

    kafka_thread.join();
  }
  catch (const std::exception &e)
  {
    std::cerr << "[FatalError] " << e.what() << std::endl;
  }

  return 0;
}