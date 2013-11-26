#include "taskpp11.h"
#include <cassert>

#include <iostream>

task_pool::~task_pool()
{
	remove_workers(M_workers.size());
	assert(!M_workers.size());
}

bool task_pool::try_pop(reference val)
{
#if TASKPP11_USE_MUTEXES
	M_mutex.lock();
	if (M_queue.size())
	{
		reference ref = M_queue.front();
		// move the object before popping queue
		val = ref;
		M_queue.pop();
		M_mutex.unlock();
		return true;
	}
	else
	{
		M_mutex.unlock();
		return false;
	}
#else
#	error Lock-free approach is yet unimplemented
#endif // TASKPP11_USE_MUTEXES
}

void task_pool::flush_tasks()
{
	auto workers = M_workers.size();
	auto expected_fence = M_fence + workers;
	for (auto i = workers; i > 0; --i)
		push(task(fence_impl, this));
	while (M_fence < expected_fence)
		std::this_thread::yield();
	/*for (auto i = workers; i > 0; --i)
		M_resume.notify_one();*/
}

void task_pool::add_workers(size_t count)
{
	std::unique_lock<std::mutex>(M_workers_mutex);
	//M_workers.reserve(M_workers.size() + count);
	for (auto i = count; i > 0; --i)
		M_workers.emplace_back(*this);
}

void task_pool::remove_workers(size_t count)
{
	if (count > 0)
	{
		std::unique_lock<std::mutex>(M_workers_mutex);
		
		auto workers = M_workers.size();
		auto expected_fence = M_fence + workers;
		auto kill_params = new kill_param[workers];
		auto worker_it = M_workers.begin();
		
		for (size_t i = 0; i < M_workers.size(); ++i, ++worker_it)
		{
			kill_params[i].first = this;
			kill_params[i].second = &(*worker_it);
			push(task(kill_fence_impl, &kill_params[i]));
		}
		
		while (M_fence < expected_fence)
			std::this_thread::yield();
		
		delete [] kill_params;
		
		// FIXME: make M_terminate and M_fence atomic?
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
	auto *pool = (task_pool *)arg;
	++pool->M_fence;
	//pool->M_resume.wait();
}

void task_pool::kill_fence_impl(void *arg)
{
	auto param = (kill_param *)arg;
	param->second->M_terminate = true;
	volatile bool *ptr = &(param->second->M_terminate);
	++param->first->M_fence;
}

void worker::thread_proc(task_pool *queue)
{
	while (!M_terminate)
	{
		task_pool::task task;
		if (queue->try_pop(task))
			task.first(task.second);
		else
			std::this_thread::yield();
	}
}
