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

#include "api.h"
#include "scene.h"
#include "camera.h"
#include "film.h"
#include "sampling.h"
#include "samplerrenderer.h"
#include "randomgen.h"
#include "context.h"
#include "core/tbbrendercommon.h"
#include "renderers/statistics/samplerstatistics.h"

using namespace lux;

//------------------------------------------------------------------------------
// TBB Variant of samplerrenderer.cpp.
//------------------------------------------------------------------------------

SamplerRenderer::SamplerRenderer() : Renderer() {
	state = INIT;

	preprocessDone = false;
	suspendThreadsWhenDone = false;

	cancelState.store(INIT);
	nThreads = 0;
	chunkSizeParam = 0;

	AddStringConstant(*this, "name", "Name of current renderer", "sampler");

	rendererStatistics = new SRStatistics(this);
}

SamplerRenderer::~SamplerRenderer() {
	boost::mutex::scoped_lock lock(classWideMutex);

	delete rendererStatistics;

	if ((state != TERMINATE) && (state != INIT))
		throw std::runtime_error("Internal error: called SamplerRenderer::~SamplerRenderer() while not in TERMINATE or INIT state.");

	if (renderThreads.size() > 0)
		throw std::runtime_error("Internal error: called SamplerRenderer::~SamplerRenderer() while list of renderThreads is not empty.");
}

Renderer::RendererType SamplerRenderer::GetType() const {
	return SAMPLER_TYPE;
}

Renderer::RendererState SamplerRenderer::GetState() const {
	boost::mutex::scoped_lock lock(classWideMutex);

	return state;
}

void SamplerRenderer::SuspendWhenDone(bool v) {
	boost::mutex::scoped_lock lock(classWideMutex);
	suspendThreadsWhenDone = v;
}

void SamplerRenderer::Render(Scene *s) {
	// Establish a single tbb::global_control for the process
	// before Embree device init, and destroyed after the render.

	TBBRenderScope scope(Context::GetActive()->GetThreadCount());

	{
		// Section under mutex.
		boost::mutex::scoped_lock lock(classWideMutex);

		scene = s;

		if (scene->IsFilmOnly()) {
			state = TERMINATE;
			cancelState.store(TERMINATE);
			return;
		}

		if (scene->lights.size() == 0) {
			LOG( LUX_SEVERE,LUX_MISSINGDATA)<< "No light sources defined in scene; nothing to render.";
			state = TERMINATE;
			cancelState.store(TERMINATE);
			return;
		}

		state = RUN;
		cancelState.store(RUN);

		// Initialize the stats.
		rendererStatistics->reset();

		// Initialize the thread's rangen.
		u_long seed = scene->seedBase - 1;
		LOG( LUX_DEBUG,LUX_NOERROR) << "Preprocess thread uses seed: " << seed;

		RandomGenerator rng(seed);

		// Integrator preprocessing.
		scene->sampler->SetFilm(scene->camera()->film);
		scene->surfaceIntegrator->Preprocess(rng, *scene);
		scene->volumeIntegrator->Preprocess(rng, *scene);
		scene->camera()->film->CreateBuffers();

		scene->surfaceIntegrator->RequestSamples(scene->sampler, *scene);
		scene->volumeIntegrator->RequestSamples(scene->sampler, *scene);

		// To support autofocus for some camera models.
		scene->camera()->AutoFocus(*scene);

		sampPos = 0;

		// Start the timer.
		rendererStatistics->start();

		// Preprocessing done.
		preprocessDone = true;
		scene->SetReady();

		// Worker slots and statistics holders.
		nThreads = Context::GetActive()->GetThreadCount();
		slots.resize(nThreads);
		for (u_int i = 0; i < nThreads; ++i) {
			SlotState &st = slots[i];
			st.rng = NULL;
			st.inited = false;
			renderThreads.push_back(new RenderThread(i, this));
		}
	}
	// Mutex is released before the render loop, so Pause/Resume/
	// Terminate do not block on rendering.

	u_int chunk = (chunkSizeParam > 0)
		? static_cast<u_int>(chunkSizeParam)
		: scene->camera()->film->GetXResolution()
			* scene->camera()->film->GetYResolution();
	if (chunk == 0)
		chunk = 1;

	// Bound the arena to nThreads so current_thread_index() is in [0, nThreads].
	tbb::task_arena arena(nThreads);

	{
		// Thread for checking write interval.
		WriteIntervalGuard writeIntervalGuard(scene->camera()->film);

		arena.execute([&]{
			while (cancelState.load() != TERMINATE) {
				if (cancelState.load() == PAUSE) {
					// Idle between chunks (on the render thread, not the GUI thread).
					boost::this_thread::sleep(boost::posix_time::milliseconds(200));
					continue;
				}

				tbb::parallel_for(tbb::blocked_range<u_int>(0, chunk),
					[this](const tbb::blocked_range<u_int> &r) {
						RenderChunk(r);
					});

				// Halt condition, checked after the barrier.
				if (scene->camera()->film->enoughSamplesPerPixel)
					break;
			}
		});
	}

	{
		boost::mutex::scoped_lock lock(renderThreadsMutex);

		for (u_int i = 0; i < renderThreads.size(); ++i)
			delete renderThreads[i];
		renderThreads.clear();

		// Change the current signal to exit in order to disable the creation
		// of new threads after this point.
		Terminate();
	}

	// Release contribution buffers and sampler state.
	for (u_int i = 0; i < slots.size(); ++i) {
		if (!slots[i].inited)
			continue;
		scene->camera()->film->contribPool->End(slots[i].sample.contribBuffer);
		slots[i].sample.contribBuffer = NULL;
		scene->sampler->FreeSample(&slots[i].sample);
		delete slots[i].rng;
		slots[i].rng = NULL;
	}
	slots.clear();

	// Flush the contribution pool.
	scene->camera()->film->contribPool->Flush();
	scene->camera()->film->contribPool->Delete();
}

void SamplerRenderer::Pause() {
	boost::mutex::scoped_lock lock(classWideMutex);
	state = PAUSE;
	cancelState.store(PAUSE);
	rendererStatistics->stop();
}

void SamplerRenderer::Resume() {
	boost::mutex::scoped_lock lock(classWideMutex);
	state = RUN;
	cancelState.store(RUN);
	rendererStatistics->start();
}

void SamplerRenderer::Terminate() {
	boost::mutex::scoped_lock lock(classWideMutex);
	state = TERMINATE;
	cancelState.store(TERMINATE);
}

//------------------------------------------------------------------------------
// RenderThread (this is only a statistics holder under TBB).
//------------------------------------------------------------------------------

SamplerRenderer::RenderThread::RenderThread(u_int index, SamplerRenderer *r) :
	n(index), renderer(r), thread(NULL), samples(0.), blackSamples(0.), blackSamplePaths(0.) {
}

SamplerRenderer::RenderThread::~RenderThread() {
	delete thread;
}

//------------------------------------------------------------------------------
// RenderChunk: one self-contained sample per index in the range.
//------------------------------------------------------------------------------

void SamplerRenderer::RenderChunk(const tbb::blocked_range<unsigned int> &r) {
	const int idx = tbb::this_task_arena::current_thread_index();
	if (idx < 0 || static_cast<unsigned int>(idx) >= nThreads)
		return;   // Never collide on slot 0!

	const u_int slot = static_cast<u_int>(idx);
	SlotState &st = slots[slot];
	RenderThread *rt = renderThreads[slot];

	if (!st.inited) {
		// Per-worker init. Seed derived from seedBase + slot.
		u_long seed = scene->seedBase + slot;
		LOG( LUX_DEBUG,LUX_NOERROR) << "Thread " << slot << " uses seed: " << seed;

		st.rng = new RandomGenerator(seed);
		st.sample.rng = st.rng;
		scene->sampler->InitSample(&st.sample);
		st.sample.contribBuffer = new ContributionBuffer(scene->camera()->film->contribPool);
		st.sample.camera = scene->camera()->Clone();
		st.sample.realTime = 0.f;
		st.inited = true;
	}

	Sample &sample = st.sample;

	for (u_int i = r.begin(); i != r.end(); ++i) {
		// Cooperative cancellation, check terminate.
		if (cancelState.load() == TERMINATE)
			return;

		if (!scene->sampler->GetNextSample(&sample)) {
			// We are done (film reached target samples per pixel).
			if (suspendThreadsWhenDone) {
				Pause();
				return;
			} else {
				Terminate();
				return;
			}
		}

		// Save ray time value.
		sample.realTime = sample.camera->GetTime(sample.time);
		// Sample camera transformation.
		sample.camera->SampleMotion(sample.realTime);

		// Sample new SWC thread wavelengths.
		sample.swl.Sample(sample.wavelengths);

		// Evaluate radiance along camera ray.
		const u_int nContribs = scene->surfaceIntegrator->Li(*scene, sample);

		// Update sample statistics.
		{
			fast_mutex::scoped_lock lockStats(rt->statLock);
			rt->blackSamples += nContribs;
			if (nContribs > 0)
				++(rt->blackSamplePaths);
			++(rt->samples);
		}

		scene->sampler->AddSample(sample);

		// Free BSDF memory from computing image sample values.
		sample.arena.FreeAll();
	}
}

Renderer *SamplerRenderer::CreateRenderer(const ParamSet &params) {
	SamplerRenderer *r = new SamplerRenderer();
	r->chunkSizeParam = params.FindOneInt("chunkSize", 0);
	return r;
}

static DynamicLoader::RegisterRenderer<SamplerRenderer> r("sampler");
