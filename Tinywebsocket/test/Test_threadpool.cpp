#include "my_threadpool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::mutex g_trace_mtx;

std::string ThreadIdToString(std::thread::id id)
{
	std::ostringstream oss;
	oss << id;
	return oss.str();
}

void TraceLine(const std::string& line)
{
	std::lock_guard<std::mutex> locker(g_trace_mtx);
	std::cout << line << std::endl;
}

int ExecuteCalcTask(char op, int lhs, int rhs)
{
	switch(op)
	{
	case '+':
		return lhs + rhs;
	case '-':
		return lhs - rhs;
	case '*':
		return lhs * rhs;
	case '/':
		return rhs == 0 ? 0 : lhs / rhs;
	default:
		return 0;
	}
}

struct CalcTask
{
	const char* name;
	int lhs;
	int rhs;
	char op;
};

void BurnCpuForFlameGraph(int rounds)
{
	volatile std::uint64_t value = 0;
	for(int i = 0; i < rounds; ++i)
	{
		value = value * 1315423911u + static_cast<std::uint64_t>(i + 1);
	}
	(void)value;
}

void TestThreadPoolFlameGraph()
{
	ThreadPool pool(8);
	std::atomic<int> done(0);
	const int taskCount = 120000;

	for(int i = 0; i < taskCount; ++i)
	{
		pool.Add_Task([&done]() {
			BurnCpuForFlameGraph(4000);
			done.fetch_add(1, std::memory_order_relaxed);
		});
	}

	while(done.load(std::memory_order_relaxed) != taskCount)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

void TestAllTasksExecuted()
{
	ThreadPool pool(4);
	const int taskCount = 1000;
	std::atomic<int> done(0);

	for(int i = 0; i < taskCount; ++i)
	{
		pool.Add_Task([&done]() {
			done.fetch_add(1, std::memory_order_relaxed);
		});
	}

	for(int i = 0; i < 200; ++i)
	{
		if(done.load(std::memory_order_relaxed) == taskCount)
		{
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	assert(done.load(std::memory_order_relaxed) == taskCount);
}

void TestConcurrentAddTask()
{
	ThreadPool pool(6);
	const int producerCount = 4;
	const int tasksPerProducer = 250;
	std::atomic<int> done(0);
	std::vector<std::thread> producers;
	producers.reserve(producerCount);

	for(int p = 0; p < producerCount; ++p)
	{
		producers.emplace_back([&pool, &done, tasksPerProducer]() {//生产者线程执行
			for(int i = 0; i < tasksPerProducer; ++i)//每个生产者线程放入250个任务
			{
				pool.Add_Task([&done]() {
					done.fetch_add(1, std::memory_order_relaxed);
				});
			}
		});
	}

	for(auto& t : producers)
	{
		t.join();
	}

	const int expected = producerCount * tasksPerProducer;
	for(int i = 0; i < 200; ++i)
	{
		if(done.load(std::memory_order_relaxed) == expected)
		{
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	assert(done.load(std::memory_order_relaxed) == expected);
}

void TestTaskCanSafelyAccessSharedState()
{
	ThreadPool pool(6);
	std::mutex mtx;
	std::vector<int> values;
	values.reserve(64);

	for(int i = 0; i < 64; ++i)
	{
		pool.Add_Task([&mtx, &values, i]() {
			std::lock_guard<std::mutex> locker(mtx);
			values.push_back(i);
		});
	}

	for(int i = 0; i < 200; ++i)
	{
		{
			std::lock_guard<std::mutex> locker(mtx);
			if(values.size() == 64)
			{
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	std::lock_guard<std::mutex> locker(mtx);
	assert(values.size() == 64);
}

void TestTaskExecutionTrace()
{
	std::atomic<int> done(0);
	const std::vector<CalcTask> tasks = {
		{"加法任务", 12, 30, '+'},
		{"减法任务", 88, 21, '-'},
		{"乘法任务", 7, 9, '*'},
		{"除法任务", 144, 12, '/'},
		{"混合任务A", 5, 5, '+'},
		{"混合任务B", 50, 8, '-'}
	};

	{
		ThreadPool pool(6, [](std::thread::id id) { //这个lamda为传入的回调函数
			TraceLine("[退出] 工作线程结束, 线程�?=" + ThreadIdToString(id));
		});

		for(const auto& task : tasks)
		{
			pool.Add_Task([&done, task]() {
				const auto workerId = ThreadIdToString(std::this_thread::get_id());
				const int result = ExecuteCalcTask(task.op, task.lhs, task.rhs);
				TraceLine("[执�?�] 线程�?=" + workerId + ", 任务=" + task.name + ", 计算=" +
					std::to_string(task.lhs) + " " + task.op + " " + std::to_string(task.rhs) +
					", 结果=" + std::to_string(result));
				done.fetch_add(1, std::memory_order_relaxed);
			});
		}

		for(int i = 0; i < 200; ++i)
		{
			if(done.load(std::memory_order_relaxed) == static_cast<int>(tasks.size()))
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		assert(done.load(std::memory_order_relaxed) == static_cast<int>(tasks.size()));
	}
}

} // namespace


int main(int argc, char* argv[])
{
	if(argc > 1)
	{
		const std::string mode = argv[1];
		if(mode == "flame" || mode == "--flamegraph")
		{
			TestThreadPoolFlameGraph();
			std::cout << "ThreadPool flame workload finished." << std::endl;
			return 0;
		}
	}

	TestAllTasksExecuted();
	TestConcurrentAddTask();
	TestTaskCanSafelyAccessSharedState();
	TestTaskExecutionTrace();

	std::cout << "All ThreadPool tests passed." << std::endl;
	return 0;
}
