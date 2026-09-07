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

#ifndef LUX_SAMPLERRENDERER_H
#define LUX_SAMPLERRENDERER_H

#include <vector>
#include <boost/thread.hpp>

#include "lux.h"
#include "renderer.h"
#include "fastmutex.h"
#include "timer.h"
#include "dynload.h"
#ifdef LUX_USE_TBB
#include <atomic>
#include "core/tbbrendercommon.h"
#include "sampling.h"
#endif

namespace lux
{

class SamplerRenderer;

//------------------------------------------------------------------------------
// SamplerRenderer
//------------------------------------------------------------------------------

class SamplerRenderer : public Renderer {
public:
	SamplerRenderer();
	~SamplerRenderer();

	RendererType GetType() const;

	RendererState GetState() const;
	void SuspendWhenDone(bool v);

	void Render(Scene *scene);

	void Pause();
	void Resume();
	void Terminate();

	friend class SRStatistics;

	static Renderer *CreateRenderer(const ParamSet &params);

private:
	//--------------------------------------------------------------------------
	// RenderThread
	//--------------------------------------------------------------------------

	class RenderThread : public boost::noncopyable {
	public:
		RenderThread(u_int index, SamplerRenderer *renderer);
		~RenderThread();

		static void RenderImpl(RenderThread *r);

		u_int  n;
		SamplerRenderer *renderer;
		boost::thread *thread; // keep pointer to delete the thread object
		double samples, blackSamples, blackSamplePaths;
		fast_mutex statLock;
	};

	void CreateRenderThread();

	//--------------------------------------------------------------------------

	mutable boost::mutex classWideMutex;
	mutable boost::mutex renderThreadsMutex;

	RendererState state;
	vector<RenderThread *> renderThreads;
	Scene *scene;

	fast_mutex sampPosMutex;
	u_int sampPos;

	// Put them last for better data alignment
	// used to suspend render threads until the preprocessing phase is done
	bool preprocessDone;
	bool suspendThreadsWhenDone;

#ifdef LUX_USE_TBB
public:
	// Cooperative cancellation state (RUN/PAUSE/TERMINATE) read by the render
	// worker between samples; written by Pause/Resume/Terminate.
	std::atomic<unsigned int> cancelState;

private:
	unsigned int nThreads;
	int chunkSizeParam;   // 0 => default (Xres * Yres, one sample per pixel)

	// Per-worker persistent state. Indexed by the TBB worker slot
	// (tbb::this_task_arena::current_thread_index()), which is guaranteed to be
	// in [0, nThreads) because the loop runs inside an explicit task_arena.
	struct SlotState {
		RandomGenerator *rng;
		Sample sample;
		bool inited;
	};
	std::vector<SlotState> slots;

	void RenderChunk(const tbb::blocked_range<unsigned int> &r);
#endif
};

}//namespace lux

#endif // LUX_SAMPLERRENDERER_H
