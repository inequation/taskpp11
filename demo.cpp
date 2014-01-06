#include <iostream>
#include <thread>
#include <chrono>
#include <utility>
#include <map>
#include <atomic>

#include "taskpp11.h"

using namespace std;
using namespace std::chrono;

void print_thread_id(void *arg)
{
	struct task_counter
	{
		size_t counter;
		size_t index;
		
		task_counter()
			: counter(0)
		{
			static atomic<size_t> global_index(0);
			index = global_index++;
		}
	};
	class task_counter_map : public map<thread::id, task_counter>
	{
	public:
		~task_counter_map()
		{
			size_t num_tasks = 0;
			for (auto it = begin(); it != end(); ++it)
			{
				cout << "Thread " << it->second.index << " completed "
					<< it->second.counter << " tasks" << endl;
				num_tasks += it->second.counter;
			}
			cout << "Total tasks processed: " << num_tasks << endl;
		}
	};
	static task_counter_map task_counters;
	
	unsigned int id = reinterpret_cast<uintptr_t>(arg);
	this_thread::sleep_for(milliseconds(rand() % 1000));
	
	{
		// make sure prints are serialized
		static mutex print_mutex;
		unique_lock<mutex> lock(print_mutex);
		auto& counter = task_counters[this_thread::get_id()];
		cout << "This thread: " << counter.index << ", task: " << id
			<< ", tasks completed so far: " << ++counter.counter << endl;
	}
}

void print_profiling_results(const task_pool& pool)
{
	auto num_workers = pool.get_worker_count();
	auto *times = new task_pool::times[num_workers];
	task_pool::times average;
	pool.sample_times(times);
	cout << "Profiling results:" << endl;
	for (size_t i = 0; i < num_workers; ++i)
	{
		const auto sum = times[i].idle + times[i].locking + times[i].busy;
		const auto scale = 100.0 / (double)sum.count();
		cout << "Thread " << i << ":\t" << fixed
			<< (double)times[i].idle.count() * scale << "% idle\t"
			<< (double)times[i].locking.count() * scale << "% locking\t"
			<< (double)times[i].busy.count() * scale << "% busy"
			<< " (times: "
			<< duration_cast<duration<double, milli>>(times[i].idle).count() << "ms idle, "
			<< duration_cast<duration<double, milli>>(times[i].locking).count() << "ms locking, "
			<< duration_cast<duration<double, milli>>(times[i].busy).count() << "ms busy)"
			<< endl;
		average.idle += times[i].idle;
		average.locking += times[i].locking;
		average.busy += times[i].busy;
	}
	average.idle /= num_workers;
	average.locking /= num_workers;
	average.busy /= num_workers;
	const auto sum = average.idle + average.locking + average.busy;
	const auto scale = 100.0 / (double)sum.count();
	cout << "Averages:\t" << fixed
		<< (double)average.idle.count() * scale << "% idle\t"
		<< (double)average.locking.count() * scale << "% locking\t"
		<< (double)average.busy.count() * scale << "% busy"
		<< " (times: "
		<< duration_cast<duration<double, milli>>(average.idle).count() << "ms idle, "
		<< duration_cast<duration<double, milli>>(average.locking).count() << "ms locking, "
		<< duration_cast<duration<double, milli>>(average.busy).count() << "ms busy)"
		<< endl;
	delete [] times;
}

void flushed_callback(void *user)
{
	cout << "Flush finished callback! " << user << endl;
	print_profiling_results(*(task_pool *)user);
}

int main(int argc, char *argv[])
{
	task_pool pool;
	const size_t num_workers = thread::hardware_concurrency();
	constexpr size_t task_batch_size = 100;
	size_t task_index = 0;
	
	cout << "Working with " << num_workers << " workers, queue synchronized by "
#if TASKPP11_USE_QUEUE_MUTEX
		"mutex"
#else
		"spinlock"
#endif
		<< endl;
	
	pool.add_workers(num_workers);

	cout << "Issuing tasks..." << endl;
	for (size_t i = 0; i < task_batch_size; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)task_index++));
	cout << "Issuing tasks done!" << endl;

	cout << "Flushing tasks..." << endl;
	pool.flush_tasks();
	cout << "Flushing tasks done!" << endl;
	
	cout << "Issuing tasks..." << endl;
	for (size_t i = 0; i < task_batch_size; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)task_index++));
	cout << "Issuing tasks done!" << endl;
	
	void *user_ptr = (void *)&pool;
	cout << "Flushing tasks with callback " << user_ptr << endl;
	pool.flush_tasks(flushed_callback, user_ptr);
	
	cout << "Issuing tasks..." << endl;
	for (size_t i = 0; i < task_batch_size; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)task_index++));
	cout << "Issuing tasks done!" << endl;

	cout << "Sleeping for 500 ms - hopefully this will make the threads start"
		<< endl;
	this_thread::sleep_for(milliseconds(500));

	cout << "Cold-blooded thread murder in progress..." << endl;
	pool.remove_workers(num_workers);
	cout << "Cold-blooded thread murder done!" << endl;
	
	cout << "Rebuilding worker pool..." << endl;
	pool.add_workers(num_workers);
	
	cout << "Issuing tasks..." << endl;
	for (size_t i = 0; i < task_batch_size; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)task_index++));
	cout << "Issuing tasks done!" << endl;
	
	cout << "Now, task_pool destructor will take care of sync." << endl;
	
	return 0;
}
