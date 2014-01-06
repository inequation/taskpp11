#include "taskpp11.h"
#include <cassert>

using namespace std;
using namespace std::chrono;
using namespace taskpp11;

task_pool::~task_pool()
{
	remove_workers(M_workers.size());
	assert(!M_workers.size());
}

bool task_pool::try_pop(reference val)
{
	M_queue_lock.lock();
	if (M_queue.size())
	{
		reference ref = M_queue.front();
		// move the object before popping queue
		val = ref;
		M_queue.pop();
		M_queue_lock.unlock();
		return true;
	}
	else
	{
		M_queue_lock.unlock();
		return false;
	}
}

void task_pool::flush_tasks(function<void (void *)> cb, void *user)
{
	// we need to lock the worker mutex, because if the number of workers
	// changes, the fence may not be detected or may be detected prematurely
	unique_lock(M_workers_mutex);
	
	// create a fence structure
	auto workers = M_workers.size();
	auto f = new fence(M_workers.size(), cb, user);
	
	for (auto i = workers; i > 0; --i)
		push(fence_impl, f);
	
	// if a callback was specified, it will be fired by the last worker, and the
	// fence object will also be freed by that thread
	if (!cb)
	{
		f->wait_for_resume();
		delete f;
	}
}

void task_pool::add_workers(size_t count)
{
	unique_lock(M_workers_mutex);
	//M_workers.reserve(M_workers.size() + count);
	for (auto i = count; i > 0; --i)
		M_workers.emplace_back(*this);
}

void task_pool::remove_workers(size_t count)
{
	if (count > 0)
	{
		// we need to lock the worker mutex, because if the number of workers
		// changes, the fence may not be detected or may be detected prematurely
		unique_lock(M_workers_mutex);
		
		// create a fence structure
		auto workers = M_workers.size();
		auto f = new fence(M_workers.size(), nullptr, this);
		
		for (auto i = workers; i > 0; --i)
			push(kill_fence_impl, f);
		
		f->wait_for_resume();
		delete f;
		
		for (auto it = M_workers.begin(); workers > 0; ++it)
		{
			if (it == M_workers.end())
				it = M_workers.begin();
			if (it->M_terminate)
			{
				if (it->joinable())
					it->join();
				it = M_workers.erase(it);
				--workers;
			}
		}
	}
}

void task_pool::fence_impl(void *arg)
{
	auto f = (fence *)arg;
	if (--f->counter == 0)
	{
		// we have zeroed this fence's counter
		f->resume.notify_all();
		if (f->callback)
		{
			f->callback(f->user);
			delete f;
		}
	}
	else
		f->wait_for_resume();
}

void task_pool::kill_fence_impl(void *arg)
{
	auto f = (fence *)arg;
	auto pool = (task_pool *)f->user;
	const auto id = this_thread::get_id();
	
	for (auto it = pool->M_workers.begin(); it != pool->M_workers.end(); ++it)
	{
		if (it->get_id() == id)
		{
			it->M_terminate = true;
			break;
		}
	}
	
	if (--f->counter == 0)
	{
		// we have zeroed this fence's counter
		f->resume.notify_all();
	}
}

#if TASKPP11_PROFILING_ENABLED
	void worker::sample_times(times& times) const
	{
		// convenience constants
		constexpr time_point epoch;
		constexpr auto zero = times::duration::zero();
		
		// sample time at entry
		const auto now = high_res_clock::now();
		// sync
		M_times_lock.lock();
		// copy internal counters
		times = M_times;
		// clear internal counters
		M_times.n.busy = M_times.n.locking = M_times.n.idle = zero;
		// flush (restart) the current state and add its elapsed time to the
		// counters
		for (times::states s = times::IDLE;
			s < times::MAX_STATES; s = (times::states)(s + 1))
		{
			if (M_start_points.t[s] != epoch)
			{
				times.t[s] += duration_cast<times::duration>
					(now - M_start_points.t[s]);
				M_start_points.t[s] = now;
				break;
			}
		}
		M_times_lock.unlock();
	}
	
	void task_pool::sample_times(times *stats) const
	{
		size_t index = 0;
		for (auto it = M_workers.begin(); it != M_workers.end(); ++it)
			it->sample_times(stats[index++]);
	}
	
	#define _TASKPP11_CHANGE_STATE(from, to)								\
		change_state<times::from, times::to>();
#else
	#define _TASKPP11_CHANGE_STATE(from, to)
#endif

void worker::thread_proc(task_pool *queue)
{
#if TASKPP11_PROFILING_ENABLED
	// initialize profiling state
	M_start_points.n.idle = high_res_clock::now();
#endif
	task_pool::task task;
	while (!M_terminate)
	{
		_TASKPP11_CHANGE_STATE(IDLE, LOCKING)
		if (queue->try_pop(task))
		{
			_TASKPP11_CHANGE_STATE(LOCKING, BUSY)
			task.first(task.second);
			_TASKPP11_CHANGE_STATE(BUSY, IDLE)
		}
		else
		{
			_TASKPP11_CHANGE_STATE(LOCKING, IDLE)
#if TASKPP11_YIELD_WHEN_IDLE
			this_thread::yield();
#endif
		}
	}
}
