/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version                                                     *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>  *
 *                                                                         *
 *   This project is based on PBRT; see http://www.pbrt.org                *
 ***************************************************************************/

// TBB implementation of scheduling::Scheduler. 

// Each Launch() is a tbb::parallel_for over worker slots, which
// returns once every worker has completed.

#include "scheduler.h"

#include <tbb/parallel_for.h>

namespace scheduling
{

Range::Range(Scheduler *sched, Thread *thread_data)
{
	scheduler = sched;
	thread = thread_data;
	current = 0;
	max = 0;
}

Scheduler::Scheduler(unsigned step)
{
	current_task = NULL;
	default_step = step;
	runState = RUNNING;
	threadsInitialized = false;
	current.store(0);
	start = 0;
	end = 0;
	step = 0;
}

Scheduler::~Scheduler()
{
}

void Scheduler::InitThreads(unsigned n, const boost::function<Thread*()> &factory)
{
	// Threads are fixed at render start.
	if (threadsInitialized)
		return;
	threadsInitialized = true;

	for (unsigned i = 0; i < n; ++i) {
		Thread *thread = factory();
		thread->inited = false;
		threads.push_back(thread);
	}
}

void Scheduler::Launch(TaskType task, unsigned b_min, unsigned b_max, unsigned force_step)
{
	start = b_min;
	end = b_max;
	current.store(b_min);
	step = force_step ? force_step : default_step;

	// One task per worker slot. Each slot owns threads[w] (persistent per-worker
	// state: photon sampler, RNG, samples).
	tbb::parallel_for(0u, static_cast<unsigned>(threads.size()),
		[this, &task](unsigned w) {
			Thread *t = threads[w];
			if (!t->inited) {
				t->Init();
				t->inited = true;
			}
			Range r(this, t);
			task(&r);
		});
}

void Scheduler::Pause()
{
	runState = PAUSED;
}

void Scheduler::Resume()
{
	runState = RUNNING;
}

void Scheduler::Stop()
{
	runState = TERMINATED;
}

void Scheduler::Done()
{
	for (unsigned i = 0; i < threads.size(); i++) {
		if (threads[i]->inited)
			threads[i]->End();
		delete threads[i];
	}
	threads.clear();
}

// Not used on the TBB path. Only here so the class links cleanly.
TaskType Scheduler::GetTask()
{
	return NULL;
}

bool Scheduler::EndTask(Thread* /*thread*/)
{
	return false;
}

}
