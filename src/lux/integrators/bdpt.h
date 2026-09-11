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

// bdpt.h*

// Bidirectional Path Tracing, LuxRender implementation of PBRTv3 semantics.
// The MIS weight math is a literal port of BidirIntegrator::WeightPath.

#ifndef LUX_BDPT_H
#define LUX_BDPT_H

#include "lux.h"
#include "sampling.h"
#include "transport.h"
#include "reflection/bxdf.h"
#include "renderinghints.h"

namespace lux
{

class BDPTVertex;

// Temporarily overwrite a scalar field and restore it on
// exit. Used by the connection code to set the connection vertices'
// flags / rr / rrR / dAWeight / dARWeight for the weight function.
template<typename T>
class ScopedAssignment {
public:
	ScopedAssignment(T &target, const T &value)
		: m_target(target), m_saved(target) {
		m_target = value;
	}
	~ScopedAssignment() { m_target = m_saved; }
private:
	// Non-copyable; a copy would double-restore.
	ScopedAssignment(const ScopedAssignment&);
	ScopedAssignment& operator=(const ScopedAssignment&);
	T &m_target;
	T m_saved;
};

// A single path vertex type covers camera, surface, light-origin and
// light-hit endpoints.
class BDPTVertex {
public:
	enum Type { CAMERA, SURFACE, LIGHT_ORIGIN, LIGHT_HIT };

	BDPTVertex()
		: type(SURFACE), cosi(0.f), coso(0.f), pdf(0.f), pdfR(0.f),
		tPdf(1.f), tPdfR(1.f), dAWeight(0.f), dARWeight(0.f),
		rr(1.f), rrR(1.f), d2(0.f), flux(0.f), bsdf(NULL),
		flags(BxDFType(0)), delta(false), light(NULL), lightGroup(0),
		single(false) {
	}

	Type type;

    // For a light origin this is the emission Le as returned by SampleL;
    // the light selection pdf is handled by the weight function.
    // PBRTv3 calls this "beta."
	SWCSpectrum flux;

	// Solid-angle pdfs:
	//   pdf  : probability of sampling wo knowing wi
	//   pdfR : probability of sampling wi knowing wo
	float cosi, coso, pdf, pdfR;

	// Pass-through probability along the segment to the next vertex.
	float tPdf, tPdfR;

	// Area-measure weighting factors for MIS.
	float dAWeight, dARWeight;

	// Russian-roulette acceptance probabilities.
	float rr, rrR;

	// Squared distance towards the next vertex along the path.
	float d2;

	BSDF *bsdf;          // Arena-allocated LuxRender BSDF.
	BxDFType flags;      // Scattering flags describing the direction sampling.
	bool delta;          // Specular/delta transport.
	Vector wi, wo;       // Towards next / previous vertex along the path.
	Point p;             // Shading point.

	const Light *light;  // Non-null for LIGHT_HIT / LIGHT_ORIGIN.
	u_int lightGroup;
	bool single;         // Dispersion single-wavelength flag (from sw).
};

// BDPTIntegrator Declarations
class BDPTIntegrator : public SurfaceIntegrator {
public:
	BDPTIntegrator(u_int ed, u_int ld, float et, float lt,
		LightsSamplingStrategy *lds, u_int src,
		LightsSamplingStrategy *lps, u_int lrc, bool dbg)
		: SurfaceIntegrator(),
		maxEyeDepth(ed), maxLightDepth(ld),
		eyeThreshold(et), lightThreshold(lt),
		lightDirectStrategy(lds), lightPathStrategy(lps),
		shadowRayCount(src), lightRayCount(lrc), debug(dbg),
		directSamplingCount(0), pathSamplingCount(0),
		eyeBufferId(0), lightBufferId(0),
		lightNumOffset(0), lightPortalOffset(0), lightPosOffset(0),
		sampleDirectOffset(0), sampleEyeOffset(0) {
		AddStringConstant(*this, "name", "Name of current surface integrator", "bdpt");
		AddIntAttribute(*this, "maxEyeDepth", "Eye path max. depth", &BDPTIntegrator::GetMaxEyeDepth);
		AddIntAttribute(*this, "maxLightDepth", "Light path max. depth", &BDPTIntegrator::GetMaxLightDepth);
	}
	virtual ~BDPTIntegrator();

	virtual u_int Li(const Scene &scene, const Sample &sample) const;
	virtual void RequestSamples(Sampler *sampler, const Scene &scene);
	virtual void Preprocess(const RandomGenerator &rng, const Scene &scene);

	static SurfaceIntegrator *CreateSurfaceIntegrator(const ParamSet &params);

	u_int maxEyeDepth, maxLightDepth;
	float eyeThreshold, lightThreshold;
	u_int sampleEyeOffset;
	u_int eyeBufferId, lightBufferId;
	vector<u_int> sampleLightOffsets;

private:
	u_int GetMaxEyeDepth() { return maxEyeDepth; }
	u_int GetMaxLightDepth() { return maxLightDepth; }

	// Pure MIS weight for the (s,t) connection. Port of
	// BidirIntegrator::WeightPath, but it reads vertex fields and
	// mutates nothing. Parameters:
	//   eye[0..nEye-1], light[0..nLight-1] are the two subpaths.
	//   pdfLightDirect: pdf of sampling the light origin by NEE.
	//   isLightDirect: self explanatory.
	float Weight(const vector<BDPTVertex> &eye, u_int nEye,
		const vector<BDPTVertex> &light, u_int nLight,
		float pdfLightDirect, bool isLightDirect) const;

	// Evaluate the contribution of connecting eye[nEye-1] to light[nLight-1].
	//   *L       : MIS-weighted radiance on success.
	//   *weight  : The MIS weight.
	//   *d2out   : Squared distance of the connection.
	//   *ewiOut  : Direction from eye[nEye-1] towards light[nLight-1].
	bool Connect(const Scene &scene, const Sample &sample,
		vector<BDPTVertex> &eye, u_int nEye,
		vector<BDPTVertex> &light, u_int nLight,
		float pdfLightDirect, bool isLightDirect,
		SWCSpectrum *L, float *weight, float *d2out, Vector *ewiOut,
		bool &single) const;

	// NEE.
	bool GetDirectLight(const Scene &scene, const Sample &sample,
		vector<BDPTVertex> &eyePath, u_int length, const Light *light,
		float u0, float u1, float portal, float lightWeight,
		float directWeight, SWCSpectrum *Ld, float *weight,
		float *d2out, Vector *ewiOut) const;

	LightsSamplingStrategy *lightDirectStrategy, *lightPathStrategy;
	u_int shadowRayCount, lightRayCount;
	u_int directSamplingCount, pathSamplingCount;
	u_int lightNumOffset, lightPortalOffset;
	u_int lightPosOffset, sampleDirectOffset;
	bool debug;
};

}//namespace lux

#endif // LUX_BDPT_H
