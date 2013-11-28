#pragma once

#if __cplusplus <= 199711L
#	error This library requires C++11 support.
#endif

#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <utility>
#include <list>
#include <atomic>
#include <condition_variable>
#include <cassert>

#define TASKPP11_USE_QUEUE_MUTEXES	1

class worker;

class task_pool
{
	public:
		typedef std::function<void (void *)>		task_function;
		typedef std::pair<task_function, void *>	task;
	protected:
		typedef std::queue<task>					queue;
	public:
		typedef queue::reference					reference;

		task_pool() : M_fence(0) {}
		task_pool(size_t pool_size) : M_fence(0) {add_workers(pool_size);}
		~task_pool();
		
		inline void push(const queue::value_type& val) {M_queue.push(val);}
		inline void push(queue::value_type&& val) {M_queue.push(val);}
		inline void push(task_function&& f, void *p) {push(task(f, p));}
		
		bool try_pop(reference val);
		void flush_tasks();
		void flush_tasks(std::function<void (void *)> callback, void *user);
		void add_workers(size_t count);
		void remove_workers(size_t count);
		
	private:
		std::list<worker>							M_workers;
		std::mutex									M_workers_mutex;
		
		queue										M_queue;
#if TASKPP11_USE_QUEUE_MUTEXES
		std::mutex									M_mutex;
#endif
		
		std::atomic<int>							M_fence;
		typedef std::unique_lock<std::mutex>		unique_lock;
		std::mutex									M_resume_mutex;
		std::condition_variable						M_resume;
		
		static void fence_impl(void *arg);
		static void kill_fence_impl(void *arg);
		
		void flush_callback_impl(std::function<void (void *)> cb, void *user);
};

class worker : private std::thread
{
	public:
		explicit worker(task_pool& in_task_pool)
			: std::thread(&worker::thread_proc, this, &in_task_pool)
			, M_terminate(false)
		{}

	private:
		friend class task_pool;

		std::atomic<bool>							M_terminate;

		void thread_proc(task_pool *queue);
};
