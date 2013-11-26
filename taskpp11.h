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
#include <cassert>

#define TASKPP11_USE_MUTEXES	1

class worker;

class task_pool
{
	public:
		typedef std::pair<std::function<void (void *)>, void *>	task;
	protected:
		typedef std::queue<task>								queue;
	public:
		typedef queue::reference								reference;

		task_pool() : M_fence(0) {}
		task_pool(size_t pool_size) : M_fence(0) {add_workers(pool_size);}
		~task_pool();
		
		inline void push(const queue::value_type& val) {M_queue.push(val);}
		inline void push(queue::value_type&& val) {M_queue.push(val);}
		
		bool try_pop(reference val);
		void flush_tasks();
		void add_workers(size_t count);
		void remove_workers(size_t count);
		
	protected:
		typedef std::pair<task_pool *, worker *>				kill_param;
		
		std::list<worker>			M_workers;
		std::mutex					M_workers_mutex;
		queue						M_queue;
#if TASKPP11_USE_MUTEXES
		std::mutex					M_mutex;
#endif
		volatile size_t				M_fence;
		
		static void fence_impl(void *arg);
		static void kill_fence_impl(void *arg);
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

		volatile bool M_terminate;

		void thread_proc(task_pool *queue);
};
