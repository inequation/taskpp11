#include <iostream>
#include <thread>
#include <chrono>
#include <utility>

#include "taskpp11.h"

void print_thread_id(void *arg)
{
	unsigned int id = reinterpret_cast<uintptr_t>(arg);
	std::cout << "This thread: " << id << " " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 1000));
}

int main(int argc, char *argv[])
{
	task_pool pool;
	
	pool.add_workers(4);

	for (size_t i = 0; i < 10; ++i)
		pool.push(task_pool::task(print_thread_id, (void *)i));

	//pool.flush_tasks();

	return 0;
}
