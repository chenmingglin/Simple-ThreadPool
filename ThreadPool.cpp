#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <atomic>
#include <map>
 /*
 *    File: ThreadPool.cpp
 *    基于C++11的线程池实现
 *    构成：
 *       1.管理者线程 ->子线程, 1个
 *           --负责控制工作线程的数量,增加或者减少
 *       2.工作线程 ->子线程, 多个
 *           --从任务队列中获取任务,执行任务
 *           --任务队列为空,则阻塞(条件变量)
 *           --线程同步(互斥锁)
 *           --当前的工作线程数量, 空闲线程的数量
 *           --最大，最小线程数量
 *       3.任务队列 ->任务, 多个 std->queue
 *           --互斥锁
 *           --条件变量
 *       4.线程开关
 *         --线程开关
  */

class ThreadPool
{
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    //构造函数min为最小线程数量, max默认为当前CPU的核数线程数量
    ThreadPool(int min = 2, int max = std::thread::hardware_concurrency());

    //析构函数
    ~ThreadPool();

    //添加任务
    template<class F, class... Args>
    void enqueue(F&& f, Args&&... args);

private:
    //管理者线程函数
    void manager_thread_func();

    //工作线程函数
    void worker_thread_func();
private:
    //管理者线程
    std::thread* manager_thread;

    //工作线程
    std::map<std::thread::id, std::thread> worker_threads_map;

    //摧毁的线程ID
    std::vector<std::thread::id> destroy_thread_id;

    //任务队列
    std::queue<std::function<void()>> task_queue;

    //互斥锁
    std::mutex mutex_;

    //条件变量
    std::condition_variable condition;

    //线程开关
    std::atomic<bool> stop_;

    //当前线程数量
    std::atomic<int> current_thread_num;

    //空闲线程数量
    std::atomic<int> idle_thread_num;

    //最大线程数量
    int max_thread_num;

    //最小线程数量
    int min_thread_num;

    //线程销毁数量
    std::atomic<int> destroy_thread_num;

    //销毁线程ID 锁;
    std::mutex destroy_thread_id_mutex;
};

ThreadPool::ThreadPool(int min, int max)
    : min_thread_num(min)
    , max_thread_num(max)
    , current_thread_num(min)
    , idle_thread_num(min)
    , stop_(false)
    , destroy_thread_num(0)
{
    //创建管理者线程
    manager_thread = new std::thread(std::bind(&ThreadPool::manager_thread_func, this));
    std::cout << "创建管理者线程ID:" << manager_thread->get_id() << std::endl;

    //创建工作线程
    for (int i = 0; i < min_thread_num; ++i)
    {
        std::thread t(std::bind(&ThreadPool::worker_thread_func, this));
        worker_threads_map.emplace(t.get_id(), std::move(t));
    }
}

void ThreadPool::manager_thread_func()
{
    while (!stop_.load())
    {
        std::cout << "管理者线程工作中-------:" << std::endl;
        //每隔3s检测一次
        std::this_thread::sleep_for(std::chrono::seconds(1));
    
        /*获取当前线程数量*/
        int curthread_num = this->current_thread_num.load();
        /*获取空闲线程数量*/
        int idlethread_num = this->idle_thread_num.load();
    
        /*判断是否需要销毁线程*/
        if (idlethread_num >= curthread_num / 2 && curthread_num >= min_thread_num)
        {
            //每次销毁两个线程
            destroy_thread_num.store(2);
            condition.notify_all();
    
            std::unique_lock<std::mutex> id_lock(destroy_thread_id_mutex);
            for (auto& t_id : destroy_thread_id) {
                auto it = worker_threads_map.find(t_id);
    
                if (it != worker_threads_map.end()) {
                    it->second.join();
                    worker_threads_map.erase(it);
                    std::cout << "线程销毁的Id: " << t_id << std::endl;
                }
            }
            destroy_thread_id.clear();
            id_lock.unlock();
        }
    
        else if (idlethread_num == 0 && curthread_num < max_thread_num)
        {
            std::thread t(std::bind(&ThreadPool::worker_thread_func, this));
            worker_threads_map.emplace(t.get_id(), std::move(t));
            ++idle_thread_num;
            ++current_thread_num;
        }
    
    }
}

void ThreadPool::worker_thread_func()
{
    for (;;) {
        /*获取任务*/
        std::unique_lock<std::mutex> lock(mutex_);
        //有任务就去去任务,没有任务阻塞 线程池销毁的时候不阻塞
        condition.wait(lock, [this] {
            return stop_.load() || !task_queue.empty();
            });

        if (stop_.load())
        {
            return;
        }

        if (destroy_thread_num.load() > 0)
        {
            --current_thread_num;
            --destroy_thread_num;
            std::cout << "线程退出的Id: " << std::this_thread::get_id() << std::endl;
            {
                std::unique_lock<std::mutex> id_lock(destroy_thread_id_mutex);
                destroy_thread_id.emplace_back(std::this_thread::get_id());
            }
            return;
        }

        auto task = std::move(task_queue.front());
        task_queue.pop();
        lock.unlock();

        /*执行任务*/
        idle_thread_num--;
        std::cout << "线程执行任务Id: " << std::this_thread::get_id() << std::endl;
        task();
        idle_thread_num++;
    }

}

template <class F, class... Args>
inline void  ThreadPool::enqueue(F&& f, Args &&...args)
{
    std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

    {
        std::unique_lock<std::mutex> lock(mutex_);
        task_queue.emplace(std::move(task));
    }
    condition.notify_one();
}


ThreadPool::~ThreadPool()
{
    stop_ = true;
    condition.notify_all();
    manager_thread->join();
    for (auto& t : worker_threads_map) {
        if (t.second.joinable()) {
            t.second.join();
        }
    }
    delete manager_thread;
}

void add(int a, int b) {
    std::cout << "a + b = " << a + b << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

int main()
{
    ThreadPool pool;

    for (int i = 0; i < 4; ++i) {
        pool.enqueue(std::bind(add, i, i + 1));
    }
    std::cin.get();
    return 0;
}