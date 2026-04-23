/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/
#pragma once
#include "math_traits.hpp"

namespace StuCanvas::utils
{
    using Task = std::function<void()>;


    class WorkStealingQueue
    {
    public:
        void push(Task task)
        {
            std::lock_guard<std::mutex> lock(mut_);
            tasks_.push_back(std::move(task));
        }


        bool pop(Task& task)
        {
            std::lock_guard<std::mutex> lock(mut_);
            if (tasks_.empty()) return false;
            task = std::move(tasks_.back());
            tasks_.pop_back();
            return true;
        }


        bool steal(Task& task)
        {
            std::lock_guard<std::mutex> lock(mut_);
            if (tasks_.empty()) return false;
            task = std::move(tasks_.front());
            tasks_.pop_front();
            return true;
        }

        bool empty()
        {
            std::lock_guard<std::mutex> lock(mut_);
            return tasks_.empty();
        }

    private:
        std::deque<Task> tasks_;
        std::mutex mut_;
    };


    class ThreadPool
    {
    public:
        static ThreadPool& instance()
        {
            static ThreadPool pool;
            return pool;
        }

        ~ThreadPool()
        {
            done_ = true;
            cv_.notify_all();
            for (auto& t : workers_)
            {
                if (t.joinable()) t.join();
            }
        }


        void ensure_threads(uint32_t num_threads)
        {
            if (num_threads == 0) num_threads = std::thread::hardware_concurrency();

            std::lock_guard<std::mutex> lock(pool_mut_);


            while (workers_.size() < num_threads)
            {
                size_t id = workers_.size();
                queues_.push_back(std::make_unique<WorkStealingQueue>());
                workers_.emplace_back(&ThreadPool::worker_thread, this, id);
            }
            active_threads_ = num_threads;
        }


        void submit_local(Task task, size_t thread_id)
        {
            queues_[thread_id % active_threads_]->push(std::move(task));
            cv_.notify_one();
        }


        void submit_global(Task task)
        {
            submit_local(std::move(task), 0);
        }

        [[nodiscard]] uint32_t get_active_threads() const { return active_threads_; }

    private:
        ThreadPool() : done_(false), active_threads_(0)
        {
        }

        void worker_thread(size_t my_id)
        {
            while (!done_)
            {
                Task task;
                bool has_task = false;


                has_task = queues_[my_id]->pop(task);


                if (!has_task)
                {
                    for (size_t i = 1; i < active_threads_; ++i)
                    {
                        size_t target_idx = (my_id + i) % active_threads_;
                        if (queues_[target_idx]->steal(task))
                        {
                            has_task = true;
                            break;
                        }
                    }
                }


                if (has_task)
                {
                    task();
                }
                else
                {
                    std::unique_lock<std::mutex> lock(mut_);
                    cv_.wait_for(lock, std::chrono::milliseconds(1), [this, my_id]
                    {
                        return done_ || !queues_[my_id]->empty();
                    });
                }
            }
        }

        std::vector<std::thread> workers_;
        std::vector<std::unique_ptr<WorkStealingQueue>> queues_;
        std::atomic<bool> done_;
        std::atomic<uint32_t> active_threads_;
        std::mutex mut_;
        std::mutex pool_mut_;
        std::condition_variable cv_;
    };


    namespace detail
    {
        template <typename IndexType, typename Func>
        struct RecursiveTask
        {
            static void execute(
                IndexType start,
                IndexType end,
                IndexType grain_size,
                Func* func_ptr,
                std::shared_ptr<std::atomic<uint32_t>> pending_tasks,
                std::promise<void>* finish_signal_ptr,
                size_t current_thread_id)
            {
                if (end - start <= grain_size)
                {
                    (*func_ptr)(start, end);


                    if (pending_tasks->fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        finish_signal_ptr->set_value();
                    }
                    return;
                }


                IndexType mid = start + (end - start) / 2;


                pending_tasks->fetch_add(1, std::memory_order_relaxed);


                auto right_task = [mid, end, grain_size, func_ptr, pending_tasks, finish_signal_ptr, current_thread_id
                    ]()
                {
                    RecursiveTask<IndexType, Func>::execute(
                        mid, end, grain_size, func_ptr, pending_tasks, finish_signal_ptr, current_thread_id);
                };

                ThreadPool::instance().submit_local(std::move(right_task), current_thread_id);


                RecursiveTask<IndexType, Func>::execute(
                    start, mid, grain_size, func_ptr, pending_tasks, finish_signal_ptr, current_thread_id);
            }
        };
    }


    template <typename IndexType, typename Func>
    void parallel_for(IndexType start, IndexType end, Func&& func, uint32_t num_threads = 0, IndexType grain_size = 0)
    {
        if (start >= end) return;


        ThreadPool& pool = ThreadPool::instance();
        pool.ensure_threads(num_threads);
        uint32_t active_threads = pool.get_active_threads();


        if (grain_size == 0)
        {
            IndexType divisor = static_cast<IndexType>(active_threads) * static_cast<IndexType>(16);
            if (divisor == 0) divisor = 1;
            grain_size = std::max(static_cast<IndexType>(1), (end - start) / divisor);
        }


        std::promise<void> finish_signal;
        std::future<void> future = finish_signal.get_future();
        auto pending_tasks = std::make_shared<std::atomic<uint32_t>>(1);


        auto root_task = [start, end, grain_size, &func, pending_tasks, &finish_signal]()
        {
            detail::RecursiveTask<IndexType, Func>::execute(
                start, end, grain_size, &func, pending_tasks, &finish_signal, 0);
        };


        pool.submit_global(std::move(root_task));


        future.wait();
    }
}
