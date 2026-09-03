/***************************************************************************
 *   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   Lux Renderer is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   Lux Renderer is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   This project is based on PBRT ; see http://www.pbrt.org               *
 *   Lux Renderer website : http://www.luxrender.net                       *
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
};

}//namespace lux

#endif // LUX_SAMPLERRENDERER_H
