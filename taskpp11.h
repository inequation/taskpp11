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
#include <vector>
#include <atomic>
#include <condition_variable>
#include <cassert>

// define this to 1 to use a mutex for queue access synchronization - it allows
// for easier algorithm sanity/correctness checking, but is less performant
#define TASKPP11_USE_QUEUE_MUTEX	0

// define this to 1 to enable profiling
#define TASKPP11_PROFILING_ENABLED	1

#if TASKPP11_PROFILING_ENABLED
	#include <chrono>
#endif

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

		task_pool() {}
		task_pool(size_t pool_size) {add_workers(pool_size);}
		~task_pool();
		
		inline void push(const queue::value_type& val)
		{
			M_queue_lock.lock();
			M_queue.push(val);
			M_queue_lock.unlock();
		}
		inline void push(queue::value_type&& val)
		{
			M_queue_lock.lock();
			M_queue.push(val);
			M_queue_lock.unlock();
		}
		inline void push(task_function&& f, void *p) {push(task(f, p));}
		
		bool try_pop(reference val);
		void flush_tasks(std::function<void (void *)> callback = nullptr,
			void *user = nullptr);
		void add_workers(size_t count);
		void remove_workers(size_t count);
		size_t get_worker_count() { return M_workers.size(); }
		
#if TASKPP11_PROFILING_ENABLED
		struct times
		{
			typedef std::chrono::high_resolution_clock::duration	duration;
			duration								idle, locking, busy;
			times()
				: idle(duration::zero())
				, locking(duration::zero())
				, busy(duration::zero())
			{}
		};
		// this samples the current time stats for all workers, then resets
		// them; stats is user-provided memory, expected to be large enough to
		// accomodate get_worker_count() of times struct instances
		void sample_times(times *stats);
#endif
		
	private:
		std::list<worker>							M_workers;
		std::mutex									M_workers_mutex;
		
#if TASKPP11_USE_QUEUE_MUTEX
		// queue access synchronization using a mutex (i.e. threads waiting for
		// lock will go to sleep while waiting)
		typedef std::mutex							queue_lock;
#else
		// queue access synchronization using a spinlock (i.e. threads waiting
		// for lock will keep spinning until they can lock)
		class spinlock
		{
			public:
				spinlock() : M_lock(false) {}
				void lock()
				{
					bool expected = false;
					while (!M_lock.compare_exchange_weak(expected, true))
						expected = false;
				}
				void unlock() { M_lock = false; }
				bool is_locked() { return M_lock; }
			private:
				std::atomic<bool> M_lock;
		};
		typedef spinlock							queue_lock;
#endif
		queue_lock									M_queue_lock;
		queue										M_queue;
		
		typedef std::unique_lock<std::mutex>		unique_lock;
		
		struct fence
		{
			// callback and its argument (both may be nullptr)
			std::function<void (void *)>			callback;
			void									*user;
			
			std::atomic<int>						counter;
			std::mutex								resume_mutex;
			std::condition_variable					resume;
			
			fence(size_t num_threads)
				: callback(nullptr)
				, user(nullptr)
				, counter(num_threads)
			{}
			fence(size_t num_threads, std::function<void (void *)> cb,
				void *user_ptr)
				: callback(cb)
				, user(user_ptr)
				, counter(num_threads)
			{}
			// this constructor isn't really used but it's needed because
			// std::atomic has a deleted copy constructor
			fence(const fence& other)
				: callback(other.callback)
				, user(other.user)
				, counter(static_cast<int>(other.counter))
			{}
			
			inline void wait_for_resume()
			{
				unique_lock lock(resume_mutex);
				resume.wait(lock);
			}
		};
		typedef std::vector<fence>					fence_vector;
		
		fence_vector								M_fences;
		
		static void fence_impl(void *arg);
		static void kill_fence_impl(void *arg);
};

class worker : private std::thread
{
#if TASKPP11_PROFILING_ENABLED
	typedef std::chrono::high_resolution_clock	high_res_clock;
	typedef high_res_clock::time_point			time_point;
	time_point									M_idle_start,
												M_locking_start,
												M_busy_start;
	task_pool::times							M_times;
#endif

	public:
		explicit worker(task_pool& in_task_pool)
			: std::thread(&worker::thread_proc, this, &in_task_pool)
			, M_terminate(false)
#if TASKPP11_PROFILING_ENABLED
			, M_idle_start(high_res_clock::duration::zero())
			, M_locking_start(high_res_clock::duration::zero())
			, M_busy_start(high_res_clock::duration::zero())
#endif
		{}

#if TASKPP11_PROFILING_ENABLED
		void sample_times(task_pool::times& times);
#endif

	private:
		friend class task_pool;

		std::atomic<bool>							M_terminate;

		void thread_proc(task_pool *queue);
};
