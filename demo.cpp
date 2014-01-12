#include <iostream>
#include <thread>
#include <chrono>
#include <utility>
#include <map>
#include <vector>
#include <atomic>
#include <limits>
#include <algorithm>

#include "taskpp11.h"

using namespace std;
using namespace std::chrono;
using namespace taskpp11;

// helper struct - leverage the constructor for thread counting
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

// helper class - leverage the destructor for print stat summary
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
	
	inline const task_counter& count()
	{
		auto& counter = (*this)[this_thread::get_id()];
		++counter.counter;
		return counter;
	}
};

static task_counter_map task_counters;

// simple task: sleep and print stats
void print_thread_id(void *arg)
{
	unsigned int id = reinterpret_cast<uintptr_t>(arg);
	this_thread::sleep_for(milliseconds(rand() % 1000));
	
	{
		// make sure prints are serialized
		static mutex print_mutex;
		unique_lock<mutex> lock(print_mutex);
		auto& counter = task_counters.count();
		cout << "This thread: " << counter.index << ", task: " << id
			<< ", tasks completed so far: " << counter.counter << endl;
	}
}

// pratcical task: find max in given iterator range, move it to first index
typedef vector<int> sorted_vector;
typedef pair<sorted_vector::iterator, sorted_vector::iterator> range;
void find_max_in_range(void *arg)
{
	auto *r = (range *)arg;
	
	auto max = *r->first;
	auto max_location = r->first;
	for (auto it = r->first; it != r->second; ++it)
	{
		if (max < *it)
		{
			max = *it;
			max_location = it;
		}
	}
	swap(*r->first, *max_location);
	task_counters.count();
}

void print_profiling_results(const task_pool& pool)
{
	auto num_workers = pool.get_worker_count();
	auto *times = new taskpp11::times[num_workers];
	taskpp11::times average;
	pool.sample_times(times);
	cout << "Profiling results:" << endl;
	for (size_t i = 0; i < num_workers; ++i)
	{
		const auto sum = times[i].n.idle + times[i].n.locking + times[i].n.busy;
		const auto scale = 100.0 / (double)sum.count();
		cout << "Thread " << i << ":\t" << fixed
			<< (double)times[i].n.idle.count() * scale << "% idle\t"
			<< (double)times[i].n.locking.count() * scale << "% locking\t"
			<< (double)times[i].n.busy.count() * scale << "% busy"
			<< " (times: "
			<< duration_cast<duration<double, milli>>(times[i].n.idle).count() << "ms idle, "
			<< duration_cast<duration<double, milli>>(times[i].n.locking).count() << "ms locking, "
			<< duration_cast<duration<double, milli>>(times[i].n.busy).count() << "ms busy)"
			<< endl;
		average.n.idle += times[i].n.idle;
		average.n.locking += times[i].n.locking;
		average.n.busy += times[i].n.busy;
	}
	average.n.idle /= num_workers;
	average.n.locking /= num_workers;
	average.n.busy /= num_workers;
	const auto sum = average.n.idle + average.n.locking + average.n.busy;
	const auto scale = 100.0 / (double)sum.count();
	cout << "Averages:\t" << fixed
		<< (double)average.n.idle.count() * scale << "% idle\t"
		<< (double)average.n.locking.count() * scale << "% locking\t"
		<< (double)average.n.busy.count() * scale << "% busy"
		<< " (times: "
		<< duration_cast<duration<double, milli>>(average.n.idle).count() << "ms idle, "
		<< duration_cast<duration<double, milli>>(average.n.locking).count() << "ms locking, "
		<< duration_cast<duration<double, milli>>(average.n.busy).count() << "ms busy)"
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
	int num_workers = -1;
	if (argc > 1)
		num_workers = atoi(argv[1]);
	if (num_workers < 1)
		num_workers = thread::hardware_concurrency();
	
	task_pool pool;
	constexpr size_t task_batch_size = 100;
	size_t task_index = 0;
	
	cout << "Working with " << num_workers << " workers, queue synchronized by "
#if TASKPP11_USE_QUEUE_MUTEX
		"mutex"
#else
	#if TASKPP11_SPINLOCK_NATIVE
		"native spinlock"
	#else
		"std::atomic spinlock"
	#endif
#endif
		", workers "
#if TASKPP11_YIELD_WHEN_IDLE
		"yielding"
#else
		"not yielding"
#endif
		" CPU time when idle" << endl;
	
	if (argc < 4)
	{
		cout << "Basic sleeper test commencing" << endl;
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

		cout << "Sleeping for 500 ms to give the workers a headstart" << endl;
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
	}
	else
	{
		int vec_size = atoi(argv[2]);
		if (vec_size < 1)
			vec_size = 1024 * 1024;
		
		int range_size = atoi(argv[3]);
		if (range_size < 1)
			range_size = 16;
		
		cout << "Sort test commencing for " << vec_size << " elements in "
			"blocks of " << range_size << endl;
		
		sorted_vector vec;
		vec.reserve(vec_size);
		
		// always come up with the same sequence
		unsigned int seed = 0xDEADBEEF;
		if (argc > 4)
			seed = atoi(argv[4]);
		srand(seed);
		
		for (int i = 0; i < vec_size; ++i)
			vec.emplace_back(rand());
		
		// find the maximum serially for correctness check
		sorted_vector::value_type max = vec[0];
		for (auto it = vec.begin(); it != vec.end(); ++it)
		{
			if (max < *it)
				max = *it;
		}
		
		// divide & conquer!
		pool.add_workers(num_workers);
		int iteration = 0;
		while (vec.size() > 1)
		{
			cout << "Iteration " << ++iteration << endl;
			auto num_ranges = (vec.size() + (range_size - 1)) / range_size;
			vector<range> ranges;
			ranges.reserve(num_ranges);
			auto range_begin = vec.begin();
			auto range_end = range_begin + range_size;
			for (size_t i = 0; i < num_ranges; ++i)
			{
				if (range_end > vec.end())
					range_end = vec.end();
				ranges.emplace_back(range_begin, range_end);
				// fire the task
				pool.push(find_max_in_range, &*ranges.rbegin());
				
				range_begin += range_size;
				range_end = range_begin + range_size;
			}
			cout << "Flushing..." << endl;
			pool.flush_tasks();
			cout << "Consolidating results" << endl;
			for (size_t i = 1; i < num_ranges; ++i)
			{
				swap(*(vec.begin() + i), *ranges[i].first);
			}
			vec.resize(num_ranges);
		}
		print_profiling_results(pool);
		cout << "Maximum found: " << vec[0] << " Actual:" << max << endl;
	}
	
	cout << "Now, task_pool destructor will take care of sync." << endl;
	
	return 0;
}
