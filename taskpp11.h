#pragma once

#if __cplusplus < 201103L && (!defined(_MSC_VER) || _MSC_VER < 1900)
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

// define this to 1 to use a mutex for queue access synchronization - it allows
// for easier algorithm sanity/correctness checking, but is less performant
#define TASKPP11_USE_QUEUE_MUTEX	0

// define this to 1 to enable profiling
#define TASKPP11_PROFILING_ENABLED	1

// define this to 1 to enable workers yielding CPU time after finding the queue
// empty; 0 will make them spin until a task comes up or the OS scheduler
// switches context
#define TASKPP11_YIELD_WHEN_IDLE	1

// define this to 1 to use native atomic intrinsics for the spinlock
#define TASKPP11_SPINLOCK_NATIVE	1

#if !TASKPP11_USE_QUEUE_MUTEX && !TASKPP11_YIELD_WHEN_IDLE
	#warning Spinlock for queue and not yielding may cause bad performance
#endif

#if TASKPP11_PROFILING_ENABLED
	#include <chrono>
#endif

#if TASKPP11_SPINLOCK_NATIVE && defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
	#include <cstdint>
#endif

namespace taskpp11
{

class worker;

// utility class of a synchronization primitive that has the threads
// attempting to lock spinning instead of sleeping
class spinlock
{
	public:
#if TASKPP11_SPINLOCK_NATIVE
	#if __GNUC__
		spinlock() : M_lock(false) {}
		void lock()
		{
			while (!__sync_bool_compare_and_swap(&M_lock, false, true));
		}
		void unlock() { M_lock = false; }
		bool is_locked() { return M_lock; }
	private:
		volatile bool						M_lock;
	#elif defined(_MSC_VER)
		spinlock() : M_lock(FALSE) {}
		void lock()
		{
			while (!InterlockedCompareExchange(&M_lock, TRUE, FALSE));
		}
		void unlock() { M_lock = false; }
		bool is_locked() { return M_lock; }
	private:
		volatile uint32_t					M_lock;
	#else
		#error Please define the spinlock for this compiler
	#endif
#else
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
		std::atomic<bool>					M_lock;
#endif
};

#if TASKPP11_PROFILING_ENABLED
// utility struct that holds the profiling times
union times
{
	typedef std::chrono::high_resolution_clock::duration	duration;
	enum states { IDLE, LOCKING, BUSY, MAX_STATES };
	
	struct named	// shut up, g++
	{
		duration						idle, locking, busy;
	};
	named								n;
	duration							t[MAX_STATES];
		
	times()
		: t{duration::zero(), duration::zero(), duration::zero()}
	{}
};
#endif

// actual task pool class
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
		size_t get_worker_count() const { return M_workers.size(); }
		
#if TASKPP11_PROFILING_ENABLED
		// this samples the current time stats for all workers, then resets
		// them; stats is user-provided memory, expected to be large enough to
		// accomodate get_worker_count() of times struct instances
		void sample_times(times *stats) const;
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
			
			size_t									counter;
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
	union start_points
	{
		struct named	// shut up, g++
		{
			mutable time_point					idle,
												locking,
												busy;
		};
		mutable named							n;
		mutable time_point						t[times::MAX_STATES];
		
		start_points()
			: t{time_point(), time_point(), time_point()}
		{}
	};
	mutable start_points						M_start_points;
	mutable times								M_times;
	mutable spinlock							M_times_lock;
#endif

	public:
		explicit worker(task_pool& in_task_pool)
			: std::thread(&worker::thread_proc, this, &in_task_pool)
			, M_terminate(false)
		{}

#if TASKPP11_PROFILING_ENABLED
		void sample_times(times& times) const;
		
		template<times::states from, times::states to>
		inline void change_state()
		{
			constexpr time_point epoch;
			auto now = high_res_clock::now();
			M_times_lock.lock();
			M_times.t[from] += std::chrono::duration_cast
				<times::duration>(now - M_start_points.t[from]);
			M_start_points.t[from] = epoch;
			M_start_points.t[to] = now;
			M_times_lock.unlock();
		}
#endif

	private:
		friend class task_pool;

		bool									M_terminate;

		void thread_proc(task_pool *queue);
};

}	// namespace taskpp11
