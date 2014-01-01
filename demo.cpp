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

void flushed_callback(void *user)
{
	cout << "Flush finished callback! " << user << endl;
}

int main(int argc, char *argv[])
{
	task_pool pool;
	const size_t num_workers = thread::hardware_concurrency();
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
	for (size_t i = 0; i < 10; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)task_index++));
	cout << "Issuing tasks done!" << endl;

	cout << "Flushing tasks..." << endl;
	pool.flush_tasks();
	cout << "Flushing tasks done!" << endl;
	
	cout << "Issuing tasks..." << endl;
	for (size_t i = 0; i < 10; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)task_index++));
	cout << "Issuing tasks done!" << endl;
	
	void *user_ptr = (void *)0xDEADBEEF;
	cout << "Flushing tasks with callback " << user_ptr << endl;
	pool.flush_tasks(flushed_callback, user_ptr);
	
	cout << "Issuing tasks..." << endl;
	for (size_t i = 0; i < 10; ++i)
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
	for (size_t i = 0; i < 10; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)task_index++));
	cout << "Issuing tasks done!" << endl;
	
	cout << "Now, task_pool destructor will take care of sync." << endl;
	
	auto *times = new task_pool::times[num_workers];
	pool.sample_times(times);
	cout << "Profiling results:" << endl;
	for (size_t i = 0; i < num_workers; ++i)
	{
		auto sum = times[i].idle + times[i].locking + times[i].busy;
		cout << "Thread " << i << ":\t"
#if 0
			<< times[i].idle * 100 / sum << "% idle\t"
			<< times[i].locking * 100 / sum << "% locking\t"
			<< times[i].busy * 100 / sum << "% busy"
#else
			<< duration_cast<microseconds>(times[i].idle).count() << "ms idle\t"
			<< duration_cast<microseconds>(times[i].locking).count() << "ms locking\t"
			<< duration_cast<microseconds>(times[i].busy).count() << "ms busy"
#endif
			<< endl;
	}
	delete [] times;
	
	return 0;
}
