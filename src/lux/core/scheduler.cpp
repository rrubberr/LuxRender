#include "scheduler.h"
#include <iostream>

namespace scheduling
{

Range::Range(Scheduler *sched, Thread *thread_data)
{
	scheduler = sched;
	thread = thread_data;
}

void Thread::Body(Thread* thread, Scheduler *scheduler)
{
	thread->Init();

	TaskType task;

	while(1)
	{
		task = scheduler->GetTask();

		if(task == NULL)
			break;
		// do the job
		Range r(scheduler, thread);

		task(&r);

		// wait for completion
		if(scheduler->EndTask(thread))
		{
			thread->End();
			return;
		}
	}

	thread->End();
	scheduler->EndTask(thread);
}

void NullTask(Range*){}

Scheduler::Scheduler(unsigned step)
{
	current_task = NULL;
	default_step = step;
	state = RUNNING;
	threadsInitialized = false;
	counter = 0;

	class NullTask
	{
		void operator()(Range*){}
	};
}

Scheduler::~Scheduler()
{
}

void Scheduler::Launch(TaskType new_task, unsigned b_min, unsigned b_max, unsigned force_step)
{
	boost::unique_lock<boost::mutex> lock(mutex);
	current_task = new_task;
	start = b_min;
	end = b_max;
	current = b_min;
	if(force_step == 0)
		step = default_step;
	else
		step = force_step;

	counter = threads.size();
	condition.notify_all();

	condition.wait(lock);
}

void Scheduler::Pause()
{
	state = PAUSED;
}

void Scheduler::Resume()
{
	state = RUNNING;
}

void Scheduler::Done()
{
	Launch(NullTask, 0, 0);
	for(unsigned i = 0; i < threads.size(); i++) {
		threads[i]->thread.join();
		delete threads[i];
	}
	threads.clear();
}

void Scheduler::InitThreads(unsigned n, const boost::function<Thread*()> &factory)
{
	boost::unique_lock<boost::mutex> lock(mutex);

	// Threads are fixed at render start.
	if (threadsInitialized)
		return;
	threadsInitialized = true;

	for (unsigned i = 0; i < n; ++i) {
		Thread *thread = factory();
		threads.push_back(thread);

		// if task is running, we need to wait for one new thread
		counter++;
		thread->thread = boost::thread(boost::bind(Thread::Body, thread, this));
	}
}

TaskType Scheduler::GetTask()
{
	// Wait for a task
	boost::unique_lock<boost::mutex> lock(mutex);
	if(!current_task)
		condition.wait(lock);

	if(current_task == NullTask)
		return NULL;

	return current_task;
}

bool Scheduler::EndTask(Thread* thread)
{
	boost::unique_lock<boost::mutex> lock(mutex);
	counter--;

	if(counter == 0)
	{
		current_task = NULL;
		condition.notify_all();
	}
	else
	{
		condition.wait(lock);
	}
	return false;
}
}
