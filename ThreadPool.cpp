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
 *    ����C++11���̳߳�ʵ��
 *    ���ɣ�
 *       1.�������߳� ->���߳�, 1��
 *           --������ƹ����̵߳�����,���ӻ��߼���
 *       2.�����߳� ->���߳�, ���
 *           --����������л�ȡ����,ִ������
 *           --�������Ϊ��,������(��������)
 *           --�߳�ͬ��(������)
 *           --��ǰ�Ĺ����߳�����, �����̵߳�����
 *           --�����С�߳�����
 *       3.������� ->����, ��� std->queue
 *           --������
 *           --��������
 *       4.�߳̿���
 *         --�߳̿���
  */

class ThreadPool
{
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    //���캯��minΪ��С�߳�����, maxĬ��Ϊ��ǰCPU�ĺ����߳�����
    ThreadPool(int min = 2, int max = std::thread::hardware_concurrency());

    //��������
    ~ThreadPool();

    //�������
    template<class F, class... Args>
    void enqueue(F&& f, Args&&... args);

private:
    //�������̺߳���
    void manager_thread_func();

    //�����̺߳���
    void worker_thread_func();
private:
    //�������߳�
    std::thread* manager_thread;

    //�����߳�
    std::map<std::thread::id, std::thread> worker_threads_map;

    //�ݻٵ��߳�ID
    std::vector<std::thread::id> destroy_thread_id;

    //�������
    std::queue<std::function<void()>> task_queue;

    //������
    std::mutex mutex_;

    //��������
    std::condition_variable condition;

    //�߳̿���
    std::atomic<bool> stop_;

    //��ǰ�߳�����
    std::atomic<int> current_thread_num;

    //�����߳�����
    std::atomic<int> idle_thread_num;

    //����߳�����
    int max_thread_num;

    //��С�߳�����
    int min_thread_num;

    //�߳���������
    std::atomic<int> destroy_thread_num;

    //�����߳�ID ��;
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
    //�����������߳�
    manager_thread = new std::thread(std::bind(&ThreadPool::manager_thread_func, this));
    std::cout << "�����������߳�ID:" << manager_thread->get_id() << std::endl;

    //���������߳�
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
        std::cout << "�������̹߳�����-------:" << std::endl;
        //ÿ��3s���һ��
        std::this_thread::sleep_for(std::chrono::seconds(1));
    
        /*��ȡ��ǰ�߳�����*/
        int curthread_num = this->current_thread_num.load();
        /*��ȡ�����߳�����*/
        int idlethread_num = this->idle_thread_num.load();
    
        /*�ж��Ƿ���Ҫ�����߳�*/
        if (idlethread_num >= curthread_num / 2 && curthread_num >= min_thread_num)
        {
            //ÿ�����������߳�
            destroy_thread_num.store(2);
            condition.notify_all();
    
            std::unique_lock<std::mutex> id_lock(destroy_thread_id_mutex);
            for (auto& t_id : destroy_thread_id) {
                auto it = worker_threads_map.find(t_id);
    
                if (it != worker_threads_map.end()) {
                    it->second.join();
                    worker_threads_map.erase(it);
                    std::cout << "�߳����ٵ�Id: " << t_id << std::endl;
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
        /*��ȡ����*/
        std::unique_lock<std::mutex> lock(mutex_);
        //�������ȥȥ����,û���������� �̳߳����ٵ�ʱ������
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
            std::cout << "�߳��˳���Id: " << std::this_thread::get_id() << std::endl;
            {
                std::unique_lock<std::mutex> id_lock(destroy_thread_id_mutex);
                destroy_thread_id.emplace_back(std::this_thread::get_id());
            }
            return;
        }

        auto task = std::move(task_queue.front());
        task_queue.pop();
        lock.unlock();

        /*ִ������*/
        idle_thread_num--;
        std::cout << "�߳�ִ������Id: " << std::this_thread::get_id() << std::endl;
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