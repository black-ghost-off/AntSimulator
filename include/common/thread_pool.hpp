#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "utils.hpp"


namespace tp
{

// Minimal thread pool used to parallelize per-ant updates.
// dispatch() splits an index range into batches, runs them on the worker
// threads and blocks until all of them are done.
class ThreadPool
{
public:
	explicit ThreadPool(uint32_t thread_count = 0)
	{
		uint32_t count = thread_count ? thread_count : std::thread::hardware_concurrency();
		count = std::max(1u, count);
		workers.reserve(count);
		for (uint32_t i(count); i--;) {
			workers.emplace_back([this]() { workerLoop(); });
		}
	}

	~ThreadPool()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			running = false;
		}
		cv_task.notify_all();
		for (std::thread& t : workers) {
			t.join();
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	[[nodiscard]]
	uint32_t getThreadCount() const
	{
		return to<uint32_t>(workers.size());
	}

	void addTask(std::function<void()> task)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			tasks.push(std::move(task));
			++remaining;
		}
		cv_task.notify_one();
	}

	void waitForCompletion()
	{
		std::unique_lock<std::mutex> lock(mutex);
		cv_done.wait(lock, [this] { return remaining == 0; });
	}

	// Runs job(start, end) over [0, count) split across the workers,
	// blocking until every batch has completed
	void dispatch(uint32_t count, const std::function<void(uint32_t, uint32_t)>& job)
	{
		if (!count) {
			return;
		}
		const uint32_t batch_count = std::min(getThreadCount(), count);
		const uint32_t batch_size  = count / batch_count;
		for (uint32_t i(0); i < batch_count; ++i) {
			const uint32_t start = i * batch_size;
			const uint32_t end   = (i == batch_count - 1) ? count : start + batch_size;
			addTask([&job, start, end] { job(start, end); });
		}
		waitForCompletion();
	}

private:
	void workerLoop()
	{
		for (;;) {
			std::function<void()> task;
			{
				std::unique_lock<std::mutex> lock(mutex);
				cv_task.wait(lock, [this] { return !tasks.empty() || !running; });
				if (!running && tasks.empty()) {
					return;
				}
				task = std::move(tasks.front());
				tasks.pop();
			}
			task();
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (--remaining == 0) {
					cv_done.notify_all();
				}
			}
		}
	}

	std::vector<std::thread>          workers;
	std::queue<std::function<void()>> tasks;
	std::mutex                        mutex;
	std::condition_variable           cv_task;
	std::condition_variable           cv_done;
	uint32_t                          remaining = 0;
	bool                              running   = true;
};

} // namespace tp
