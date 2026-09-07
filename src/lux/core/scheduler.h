#pragma once

#include <vector>
#include <algorithm>

#ifdef LUX_USE_TBB
#include <atomic>
#include <boost/function.hpp>
#else
#include <boost/thread.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/bind.hpp>
#include <boost/version.hpp>
#include <boost/function.hpp>

#include <boost/interprocess/detail/atomic.hpp>

#if (BOOST_VERSION < 104800)
using boost::interprocess::detail::atomic_inc32;
#else
using boost::interprocess::ipcdetail::atomic_inc32;
#endif
#endif

/*
 * TODO:
 *
 * - Better documentation of API
 * - Pause/Resume function
 *   - use a barrier instead of a crappy sleep
 *   - should this code move at the end of each blocks ?
*/

namespace scheduling
{

class Scheduler;
class Thread;
class Range;

class Thread
{
public:
	virtual void Init() {}
	virtual void End() {}
	virtual ~Thread() {};

friend class Scheduler;
friend class Range;

private:
#ifdef LUX_USE_TBB
	// TBB path: no per-thread OS thread. Init() is invoked lazily by
	// Scheduler::Launch (once per worker slot); End() by Scheduler::Done().
	bool inited;
#else
	static void Body(Thread* thread, Scheduler *scheduler);

	boost::thread thread;
#endif
};

typedef boost::function<void(Range *range)> TaskType;

class Scheduler
{
public:
	Scheduler(unsigned step);
	~Scheduler();

	void Launch(TaskType task, unsigned b_min, unsigned b_max, unsigned force_step=0);

	void Pause();
	void Resume();
	void Stop();
	void Done();

	// Spawn n worker threads before the first Launch().
	void InitThreads(unsigned n, const boost::function<Thread*()> &factory);
	unsigned ThreadCount() const
	{
		return threads.size();
	}

friend class Thread;
friend class Range;

private:
#ifdef LUX_USE_TBB
	enum RunState {RUNNING, PAUSED, TERMINATED};
	std::atomic<int> runState;
#else
	enum {PAUSED, RUNNING} state;
#endif

	TaskType GetTask();

	bool EndTask(Thread* thread);

	std::vector<Thread*> threads;
	bool threadsInitialized;

	TaskType current_task;

#ifndef LUX_USE_TBB
	boost::mutex mutex;
	boost::condition_variable condition;
	unsigned counter;
#endif

	unsigned start;
	unsigned end;
#ifdef LUX_USE_TBB
	std::atomic<unsigned> current;
#else
	unsigned current;
#endif
	unsigned step;
	unsigned default_step;
};

class Range
{
public:
	unsigned begin()
	{
		return atomic_init();
	}

	unsigned end()
	{
		return ~0u;
	}

	unsigned next()
	{
		if(++current < max)
			return current;

#ifdef LUX_USE_TBB
		// Pause is handled between passes by the renderer; no sleep here.
		// Termination is handled inside atomic_init() (returns end()).
		return atomic_init();
#else
		// handle pause
		while (scheduler->state == Scheduler::PAUSED)
		{
			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		return atomic_init();
#endif
	}

	// public for thread local data access
	Thread *thread;

friend class Thread;
friend class Scheduler;

private:
	unsigned atomic_init()
	{
#ifdef LUX_USE_TBB
		if(scheduler->runState == Scheduler::TERMINATED)
			return end();
		unsigned new_value = scheduler->step * scheduler->current.fetch_add(1);
#else
		unsigned new_value = scheduler->step * atomic_inc32(&scheduler->current);
#endif

		if(new_value < scheduler->end)
		{
			max = std::min(scheduler->end, new_value + scheduler->step);
			current = new_value;
			return new_value;
		}
		return end();
	}

	Range(Scheduler *sched, Thread *thread_data);

	unsigned current;
	unsigned max;

	Scheduler *scheduler;
};

}
