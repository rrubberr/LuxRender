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

// bdpt.cpp*

#include "bdpt.h"
#include "reflection/bxdf.h"
#include "light.h"
#include "camera.h"
#include "sampling.h"
#include "scene.h"
#include "paramset.h"
#include "dynload.h"
#include "core/partialcontribution.h"

using namespace lux;

namespace {

// Russian-roulette start depth and pass-through guard.
static const u_int passThroughLimit = 10000;
static const u_int rrStart = 3;

// Used for the second-to-last vertex densities, when
// the corresponding subpath has length >= 2.
template<typename T>
class OptAssign {
public:
	OptAssign(T *target, const T &value)
		: m_target(target), m_saved(), m_active(false) {
		if (m_target) {
			m_saved = *m_target;
			*m_target = value;
			m_active = true;
		}
	}
	~OptAssign() {
		if (m_active)
			*m_target = m_saved;
	}
private:
	OptAssign(const OptAssign&);
	OptAssign& operator=(const OptAssign&);
	T *m_target;
	T m_saved;
	bool m_active;
};

// Splat a contribution that lands on the camera (t=1 / eye-vertex) through
// the camera's GetSamplePosition.
inline bool EyeConnect(const Sample &sample, const Point &p,
	const Vector &wi, float distance, const XYZColor &color, float alpha,
	float weight, u_int bufferId, u_int groupId) {
	float x, y;
	if (!sample.camera->GetSamplePosition(p, wi, distance, &x, &y))
		return false;
	sample.AddContribution(x, y, color, alpha, distance, weight,
		bufferId, groupId);
	return true;
}

}//namespace

//------------------------------------------------------------------------------
// Sample layout
//------------------------------------------------------------------------------

void BDPTIntegrator::RequestSamples(Sampler *sampler, const Scene &scene)
{
	directSamplingCount = lightDirectStrategy->GetSamplingLimit(scene);
	pathSamplingCount = lightPathStrategy->GetSamplingLimit(scene);
	lightNumOffset = sampler->Add1D(pathSamplingCount);
	lightPortalOffset = sampler->Add1D(pathSamplingCount * lightRayCount);
	lightPosOffset = sampler->Add2D(pathSamplingCount * lightRayCount);
	vector<u_int> structure;
	// Direct lighting samples.
	for (u_int i = 0; i < directSamplingCount; ++i) {
		structure.push_back(1); // Light source sample.
		for (u_int j = 0; j < shadowRayCount; ++j) {
			structure.push_back(2); // Light position.
			structure.push_back(1); // Light portal.
		}
	}
	sampleDirectOffset = sampler->AddxD(structure, maxEyeDepth);
	structure.clear();
	// Eye subpath samples.
	structure.push_back(1); // Continue eye.
	structure.push_back(2); // Bsdf sampling for eye path.
	structure.push_back(1); // Bsdf component for eye path.
	structure.push_back(1); // Scattering.
	sampleEyeOffset = sampler->AddxD(structure, maxEyeDepth);
	structure.clear();
	// Light subpath samples.
	const bool initOffsets = sampleLightOffsets.empty();
	structure.push_back(1); // Continue light.
	structure.push_back(2); // Bsdf sampling for light path.
	structure.push_back(1); // Bsdf component for light path.
	structure.push_back(1); // Scattering.
	for (u_int i = 0; i < pathSamplingCount * lightRayCount; ++i) {
		const u_int lightOffset = sampler->AddxD(structure, maxLightDepth);
		if (initOffsets) {
			// Only initialize once.
			sampleLightOffsets.push_back(lightOffset);
		}
	}
}

void BDPTIntegrator::Preprocess(const RandomGenerator &rng, const Scene &scene)
{
	BufferOutputConfig config = BUF_FRAMEBUFFER;
	if (debug)
		config = BufferOutputConfig(config | BUF_STANDALONE);
	BufferType type = BUF_TYPE_PER_PIXEL;
	scene.sampler->GetBufferType(&type);
	eyeBufferId = scene.camera()->film->RequestBuffer(type, config, "eye");
	lightBufferId = scene.camera()->film->RequestBuffer(BUF_TYPE_PER_SCREEN,
		config, "light");
	lightDirectStrategy->Init(scene);
	lightPathStrategy->Init(scene);
}

BDPTIntegrator::~BDPTIntegrator()
{
	delete lightDirectStrategy;
	delete lightPathStrategy;
}

//------------------------------------------------------------------------------
// MIS weight
//------------------------------------------------------------------------------

float BDPTIntegrator::Weight(const vector<BDPTVertex> &eye, u_int nEye,
	const vector<BDPTVertex> &light, u_int nLight,
	float pdfLightDirect, bool isLightDirect) const
{
	// Reference weight of the current path without direct sampling.
	const float pBase = (nLight == 1 && isLightDirect) ?
		fabsf(light[0].dAWeight) / pdfLightDirect : 1.f;
	float weight = 1.f, p = pBase;

	// Direct lighting. Can also be obtained through connection to
	// the light vertex with normal sampling.
	if (nLight == 1) {
		if (isLightDirect) {
			if ((light[0].flags & BSDF_SPECULAR) == 0 && maxLightDepth > 0 &&
				light[0].dAWeight > 0.f)
				weight += pBase * pBase;
		} else if (light[0].dAWeight > 0.f) {
			// eye-hits-light is impossible for delta lights (neg dAWeight)
			const float pDirect = pdfLightDirect / fabsf(light[0].dAWeight);
			weight += pDirect * pDirect;
		}
	}
	// Direct lighting when the eye path hits a light (eye path has >= 2 vertices;
	// the light vertex cannot be specular or the eye path gets no light).
	if (nLight == 0 && (eye[nEye - 2].flags & BSDF_SPECULAR) == 0) {
		float pDirect = pdfLightDirect / eye[nEye - 1].dARWeight;
		if (nEye > rrStart + 1)
			pDirect /= eye[nEye - 2].rrR;
		weight += pDirect * pDirect;
	}
	// Extend light path toward the eye path.
	const u_int nLightExt = min(nEye, maxLightDepth - min(maxLightDepth, nLight));
	for (u_int i = 1; i <= nLightExt; ++i) {
		if (!(eye[nEye - i].dARWeight > 0.f && eye[nEye - i].dAWeight > 0.f))
			break;
		p *= eye[nEye - i].dAWeight / eye[nEye - i].dARWeight;
		if (nEye - i > rrStart)
			p /= eye[nEye - i - 1].rrR;
		if (nLight + i > rrStart + 1) {
			if (i == 1)
				p *= light[nLight - 1].rr;
			else
				p *= eye[nEye - i + 1].rr;
		}
		if ((eye[nEye - i].flags & BSDF_SPECULAR) == 0 &&
			(i == nEye || (eye[nEye - i - 1].flags & BSDF_SPECULAR) == 0))
			weight += p * p;
	}
	// Extend eye path toward the light path.
	p = pBase;
	const u_int nEyeExt = min(nLight, maxEyeDepth - min(maxEyeDepth, nEye));
	for (u_int i = 1; i <= nEyeExt; ++i) {
		if (!(light[nLight - i].dARWeight > 0.f && light[nLight - i].dAWeight > 0.f))
			break;
		p *= light[nLight - i].dARWeight / light[nLight - i].dAWeight;
		if (nLight - i > rrStart)
			p /= light[nLight - i - 1].rr;
		if (nEye + i > rrStart + 1) {
			if (i == 1)
				p *= eye[nEye - 1].rrR;
			else
				p *= light[nLight - i + 1].rrR;
		}
		if ((light[nLight - i].flags & BSDF_SPECULAR) == 0 &&
			(i == nLight || (light[nLight - i - 1].flags & BSDF_SPECULAR) == 0))
			weight += p * p;
		// Direct lighting when path has >= 2 vertices. The second vertex must be
		// non-specular. Exclude delta lights.
		if (i == nLight - 1 && (light[1].flags & BSDF_SPECULAR) == 0 &&
			light[0].dAWeight > 0.f) {
			const float pDirect = p * pdfLightDirect / fabsf(light[0].dAWeight);
			weight += pDirect * pDirect;
		}
	}
	return weight;
}

//------------------------------------------------------------------------------
// Connection core
//------------------------------------------------------------------------------

bool BDPTIntegrator::Connect(const Scene &scene, const Sample &sample,
	vector<BDPTVertex> &eye, u_int nEye,
	vector<BDPTVertex> &light, u_int nLight,
	float pdfLightDirect, bool isLightDirect,
	SWCSpectrum *L, float *weight, float *d2out, Vector *ewiOut,
	bool &single) const
{
	static const float epsilon = MachineEpsilon::E(1.f);
	if (nLight <= 0 || nEye <= 0)
		return false;
	const SpectrumWavelengths &sw(sample.swl);
	*weight = 0.f;

	BDPTVertex &eyeV(eye[nEye - 1]);
	BDPTVertex &lightV(light[nLight - 1]);

	// Locally revert single. Connections may narrow the wavelength
	// set, but not for future evaluations.
	ContextSingle ctx(sw);
	sw.single = false;

	// Check connectability.
	const Vector ewi(Normalize(lightV.p - eyeV.p));
	const SWCSpectrum ef(eyeV.bsdf->F(sw, ewi, eyeV.wo, true,
		BxDFType(~BSDF_SPECULAR)));
	if (ef.Black())
		return false;
	const Vector lwo(-ewi);
	const SWCSpectrum lf(lightV.bsdf->F(sw, lightV.wi, lwo, false,
		BxDFType(~BSDF_SPECULAR)));
	if (lf.Black())
		return false;
	const float epdfR = eyeV.bsdf->Pdf(sw, eyeV.wo, ewi, BxDFType(~BSDF_SPECULAR));
	const float lpdf = lightV.bsdf->Pdf(sw, lightV.wi, lwo, BxDFType(~BSDF_SPECULAR));
	float ltPdf = 1.f;
	float etPdfR = 1.f;
	const Volume *volume = eyeV.bsdf->GetVolume(ewi);
	if (!volume)
		volume = lightV.bsdf->GetVolume(lwo);
	const bool eScat = eyeV.bsdf->dgShading.scattered;
	const bool lScat = lightV.bsdf->dgShading.scattered;
	if (!scene.Connect(sample, volume, eScat, lScat, eyeV.p, lightV.p,
		nEye == 1, L, &ltPdf, &etPdfR))
		return false;
	const float d2 = DistanceSquared(eyeV.p, lightV.p);
	if (d2 < max(MachineEpsilon::E(eyeV.p), MachineEpsilon::E(lightV.p)))
		return false;
	*L *= lightV.flux * lf * ef * eyeV.flux / d2;
	if (L->Black())
		return false;

	// The transient fields below are what EvalPath manually saves/restores.
    // Here they restored on return.
	const float ecosi = AbsDot(ewi, eyeV.bsdf->ng);
	const float epdf = eyeV.bsdf->Pdf(sw, ewi, eyeV.wo, BxDFType(~BSDF_SPECULAR));
	const float eyeV_rr = (nEye == 1) ? 1.f :
		(ecosi * epdf > epsilon ?
			min(1.f, max(lightThreshold, ef.Filter(sw) * eyeV.coso / (ecosi * epdf))) :
			0.f);
	const float eyeV_rrR = (epdfR > epsilon) ?
		min(1.f, max(eyeThreshold, ef.Filter(sw) / epdfR)) : 0.f;
	const float eyeV_dAWeight = lpdf * ltPdf / d2 * (eScat ? 1.f : ecosi);

	const float lcoso = AbsDot(lwo, lightV.bsdf->ng);
	const float lpdfR = lightV.bsdf->Pdf(sw, lwo, lightV.wi, BxDFType(~BSDF_SPECULAR));
	const float lightV_rr = (lpdf > epsilon) ?
		min(1.f, max(lightThreshold, lf.Filter(sw) / lpdf)) : 0.f;
	const float lightV_rrR = (nLight == 1) ? 1.f :
		(lcoso * lpdfR > epsilon ?
			min(1.f, max(eyeThreshold, lf.Filter(sw) * lightV.cosi / (lcoso * lpdfR))) :
			0.f);
	const float lightV_dARWeight = epdfR * etPdfR / d2 * (lScat ? 1.f : lcoso);

	// Continuation densities for the second-to-last vertices (the i==2 terms
	// Weight reads when extending a subpath).
	BDPTVertex *eyePrev = (nEye > 1) ? &eye[nEye - 2] : NULL;
	BDPTVertex *lightPrev = (nLight > 1) ? &light[nLight - 2] : NULL;
	const float eyePrev_dAWeight = eyePrev ?
		(epdf * eyeV.tPdf / eyePrev->d2 *
			(eyePrev->bsdf->dgShading.scattered ? 1.f : eyePrev->cosi)) : 0.f;
	const float lightPrev_dARWeight = lightPrev ?
		(lpdfR * lightV.tPdfR / lightPrev->d2 *
			(lightPrev->bsdf->dgShading.scattered ? 1.f : lightPrev->coso)) : 0.f;

	// Apply all transient assignments; they unwind automatically.
	ScopedAssignment<float> a1(eyeV.rr, eyeV_rr);
	ScopedAssignment<float> a2(eyeV.rrR, eyeV_rrR);
	ScopedAssignment<float> a3(eyeV.dAWeight, eyeV_dAWeight);
	ScopedAssignment<float> a4(lightV.rr, lightV_rr);
	ScopedAssignment<float> a5(lightV.rrR, lightV_rrR);
	ScopedAssignment<float> a6(lightV.dARWeight, lightV_dARWeight);
	OptAssign<float> a7(eyePrev ? &eyePrev->dAWeight : NULL, eyePrev_dAWeight);
	OptAssign<float> a8(lightPrev ? &lightPrev->dARWeight : NULL, lightPrev_dARWeight);

	const float w = 1.f / Weight(eye, nEye, light, nLight,
		pdfLightDirect, isLightDirect);
	*weight = w;
	*L *= w;

	// Return connection data without storing it on the vertex.
	*d2out = d2;
	if (ewiOut)
		*ewiOut = ewi;

	// State depends on both vertices + the F-evaluation single flag.
	single = sw.single || eyeV.single || lightV.single;
	return true;
}

//------------------------------------------------------------------------------
// s=1 next-event estimation
//------------------------------------------------------------------------------

bool BDPTIntegrator::GetDirectLight(const Scene &scene, const Sample &sample,
	vector<BDPTVertex> &eyePath, u_int length, const Light *light,
	float u0, float u1, float portal, float lightWeight, float directWeight,
	SWCSpectrum *Ld, float *weight, float *d2out, Vector *ewiOut) const
{
	vector<BDPTVertex> lightPath(1);
	BDPTVertex &vE(eyePath[length - 1]);
	BDPTVertex &vL(lightPath[0]);
	float ePdfDirect;
	if (!light->SampleL(scene, sample, vE.p, u0, u1, portal,
		&vL.bsdf, &vL.dAWeight, &ePdfDirect, Ld))
		return false;
	vL.type = BDPTVertex::LIGHT_ORIGIN;
	vL.p = vL.bsdf->dgShading.p;
	vL.wi = Vector(vL.bsdf->dgShading.nn);
	vL.cosi = AbsDot(vL.wi, vL.bsdf->ng);
	vL.dAWeight *= lightWeight;
	vL.flux = SWCSpectrum(1.f / directWeight);
	vL.tPdf = 1.f;
	vL.tPdfR = 1.f;
	vL.single = sample.swl.single;
	if (light->IsDeltaLight()) {
		// Negative dAWeight signals a delta light to Weight; mirror on
		// dARWeight so the eye-hits-light strategy is excluded.
		vL.delta = true;
		vL.dAWeight = -vL.dAWeight;
		vL.dARWeight = -fabsf(vL.dAWeight);
	} else {
		vL.dARWeight = fabsf(vL.dAWeight);
	}
	ePdfDirect *= directWeight;
	vE.tPdf = ePdfDirect;
	bool single;
	if (!Connect(scene, sample, eyePath, length, lightPath, 1,
		ePdfDirect, true, Ld, weight, d2out, ewiOut, single))
		return false;
	return true;
}

//------------------------------------------------------------------------------
// Main integrator entry point
//------------------------------------------------------------------------------

u_int BDPTIntegrator::Li(const Scene &scene, const Sample &sample) const
{
	u_int nrContribs = 0;
	// Allocate at least 1 eye vertex slot for the camera vertex so that
	// light-to-camera connections work.
	vector<BDPTVertex> eyePath(max(maxEyeDepth, 1u)),
		lightPath(max(maxLightDepth, 1u));
	const u_int nGroups = scene.lightGroups.size();
	const u_int numberOfLights = scene.lights.size();
	if (numberOfLights == 0)
		return nrContribs;
	const SpectrumWavelengths &sw(sample.swl);

	PartialContribution partialContribution(nGroups);
	float alpha = 1.f;

	// Sample eye subpath origin.
	const float posX = sample.camera->IsLensBased() ? sample.lensU : sample.imageX;
	const float posY = sample.camera->IsLensBased() ? sample.lensV : sample.imageY;
	if (!sample.camera->SampleW(sample.arena, sw, scene,
		posX, posY, .5f, &eyePath[0].bsdf, &eyePath[0].dARWeight,
		&eyePath[0].flux))
		return nrContribs;
	BDPTVertex &eye0(eyePath[0]);
	eye0.type = BDPTVertex::CAMERA;
	eye0.p = eye0.bsdf->dgShading.p;
	eye0.wo = Vector(eye0.bsdf->dgShading.nn);
	eye0.coso = AbsDot(eye0.wo, eye0.bsdf->ng);
	// The camera has no prior vertex through which a light path could extend.
	eye0.dARWeight = 0.f;
	eye0.single = sw.single;
	u_int nEye = 1;

	// Eye-path direct lighting for maxEyeDepth > 0.
	if (maxEyeDepth > 0) {
		const float *directData0 = sample.sampler->GetLazyValues(sample,
			sampleDirectOffset, 0);
		for (u_int l = 0; l < directSamplingCount; ++l) {
			const u_int offset = l * (1 + shadowRayCount * 3);
			SWCSpectrum Ld;
			float dWeight, dPdf, d2;
			Vector ewi;
			float portal = directData0[offset];
			const Light *light = lightDirectStrategy->SampleLight(scene, l,
				&portal, &dPdf);
			if (!light)
				break;
			dPdf *= shadowRayCount;
			const float lPdf = lightPathStrategy->Pdf(scene, light) *
				lightRayCount;
			for (u_int s = 0; s < shadowRayCount; ++s) {
				const u_int offset2 = offset + s * 3 + 1;
				if (GetDirectLight(scene, sample, eyePath, 1, light,
					directData0[offset2], directData0[offset2 + 1],
					directData0[offset2 + 2], lPdf, dPdf,
					&Ld, &dWeight, &d2, &ewi)) {
					if (light->IsEnvironmental()) {
						if (EyeConnect(sample, eye0.p, ewi, INFINITY,
							XYZColor(sw, Ld), 0.f, dWeight, lightBufferId,
							light->group))
							++nrContribs;
					} else {
						if (EyeConnect(sample, eye0.p, ewi, sqrtf(d2),
							XYZColor(sw, Ld), 1.f, dWeight, lightBufferId,
							light->group))
							++nrContribs;
					}
				}
			}
		}
	}

	bool eyePathTraced = false;
	float d = INFINITY;
	const float lensU = sample.camera->IsLensBased() ? sample.imageX : sample.lensU;
	const float lensV = sample.camera->IsLensBased() ? sample.imageY : sample.lensV;

	// Eye subpath tracing (needs maxEyeDepth > 1 for a surface vertex).
	if (maxEyeDepth > 1) {
		SWCSpectrum f0;
		if (eye0.bsdf->SampleF(sw, eye0.wo, &eye0.wi, lensU, lensV, .5f,
			&f0, &eye0.pdfR, BSDF_ALL, &eye0.flags, &eye0.pdf, true)) {
			eye0.cosi = AbsDot(eye0.wi, eye0.bsdf->ng);
			eye0.rr = min(1.f, max(lightThreshold,
				f0.Filter(sw) * eye0.coso / eye0.cosi));
			eye0.rrR = min(1.f, max(eyeThreshold, f0.Filter(sw)));
			Ray ray(eye0.p, eye0.wi);
			ray.time = sample.realTime;
			sample.camera->ClampRay(ray);
			Intersection isect;
			eyePath[nEye].flux = eye0.flux * f0;

			const Volume *volume = eye0.bsdf->GetVolume(ray.d);
			bool scattered = eye0.bsdf->dgShading.scattered;
			for (u_int sampleIndex = 1; sampleIndex < maxEyeDepth; ++sampleIndex) {
				const float *data = sample.sampler->GetLazyValues(sample,
					sampleEyeOffset, sampleIndex);
				BDPTVertex &v = eyePath[nEye];
				BDPTVertex &vp = eyePath[nEye - 1];
				float spdf, spdfR;
				if (!scene.Intersect(sample, volume, scattered, ray, data[4],
					&isect, &v.bsdf, &spdfR, &spdf, &v.flux)) {
					v.flux /= spdfR;
					// Reinitalize ray origin to the previous non-passthrough
					// intersection.
					ray.o = vp.p;
					for (u_int lightNumber = 0; lightNumber < scene.lights.size(); ++lightNumber) {
						const Light *light = scene.lights[lightNumber].get();
						if (!light->IsEnvironmental())
							continue;
						float ePdfDirect;
						SWCSpectrum Le(v.flux);
						if (!light->Le(scene, sample, ray, &v.bsdf,
							&v.dAWeight, &ePdfDirect, &Le))
							continue;
						v.type = BDPTVertex::LIGHT_HIT;
						v.light = light;
						v.lightGroup = light->group;
						v.wo = -ray.d;
						v.flags = BxDFType(~BSDF_SPECULAR);
						v.p = v.bsdf->dgShading.p;
						v.coso = AbsDot(v.wo, v.bsdf->ng);
						vp.d2 = DistanceSquared(vp.p, v.p);
						v.dARWeight = vp.pdfR * vp.tPdfR * spdfR / vp.d2;
						if (!v.bsdf->dgShading.scattered)
							v.dARWeight *= v.coso;
						v.pdf = v.bsdf->Pdf(sw, Vector(v.bsdf->dgShading.nn),
							v.wo);
						v.dAWeight *= lightPathStrategy->Pdf(scene,
							lightNumber) * lightRayCount;
						ePdfDirect *= lightDirectStrategy->Pdf(scene,
							lightNumber) * shadowRayCount;
						vp.dAWeight = v.pdf * v.tPdf * spdf / vp.d2;
						if (!vp.bsdf->dgShading.scattered)
							vp.dAWeight *= vp.cosi;
						vector<BDPTVertex> path(0);
						const float w = Weight(eyePath, nEye + 1, path, 0,
							ePdfDirect, false);
						Le /= w;
						partialContribution.Add(sw, Le, light->group, 1.0f / w);
						++nrContribs;
					}
					if (nEye == 1) {
						// Remove directly visible environment for compositing.
						alpha = 0.f;
						eye0.d2 = INFINITY;
					}
					break;
				}

				scattered = v.bsdf->dgShading.scattered;
				v.type = BDPTVertex::SURFACE;
				v.flux /= spdfR;
				vp.tPdfR *= spdfR;
				v.tPdf *= spdf;
				v.wo = -ray.d;
				v.p = isect.dg.p;
				v.coso = AbsDot(v.wo, v.bsdf->ng);
				vp.d2 = DistanceSquared(vp.p, v.p);
				v.dARWeight = vp.pdfR * vp.tPdfR / vp.d2;
				if (!scattered)
					v.dARWeight *= v.coso;
				v.single = sw.single;
				++nEye;

				// Test intersection with a light source.
				SWCSpectrum Ll(v.flux);
				BSDF *eBsdf;
				float ePdfDirect;
				if (isect.Le(sample, ray, &eBsdf, &v.dAWeight,
					&ePdfDirect, &Ll)) {
					ray.o = vp.p;
					v.type = BDPTVertex::LIGHT_HIT;
					v.light = isect.arealight;
					v.lightGroup = isect.arealight->group;
					v.flags = BxDFType(~BSDF_SPECULAR);
					v.pdf = eBsdf->Pdf(sw, Vector(eBsdf->dgShading.nn),
						v.wo, v.flags);
					v.dAWeight *= lightPathStrategy->Pdf(scene,
						isect.arealight) * lightRayCount;
					ePdfDirect *= lightDirectStrategy->Pdf(scene,
						isect.arealight) * shadowRayCount;
					vp.dAWeight = v.pdf * v.tPdf / vp.d2;
					if (!vp.bsdf->dgShading.scattered)
						vp.dAWeight *= vp.cosi;
					vector<BDPTVertex> path(0);
					const float w = Weight(eyePath, nEye, path, 0,
						ePdfDirect, false);
					Ll /= w;
					partialContribution.Add(sw, Ll, isect.arealight->group,
						1.0f / w);
					++nrContribs;
				}

				// Direct lighting at this eye vertex.
				const float *directData = sample.sampler->GetLazyValues(sample,
					sampleDirectOffset, sampleIndex);
				for (u_int l = 0; l < directSamplingCount; ++l) {
					const u_int offset = l * (1 + shadowRayCount * 3);
					SWCSpectrum Ld;
					float dWeight, dPdf, d2;
					float portal = directData[offset];
					const Light *directLight =
						lightDirectStrategy->SampleLight(scene, l, &portal,
						&dPdf);
					if (!directLight)
						break;
					dPdf *= shadowRayCount;
					const float lPdf = lightPathStrategy->Pdf(scene,
						directLight) * lightRayCount;
					for (u_int s = 0; s < shadowRayCount; ++s) {
						const u_int offset2 = offset + s * 3 + 1;
						if (GetDirectLight(scene, sample, eyePath, nEye,
							directLight, directData[offset2],
							directData[offset2 + 1], directData[offset2 + 2],
							lPdf, dPdf, &Ld, &dWeight, &d2, NULL)) {
							partialContribution.Add(sw, Ld,
								directLight->group, dWeight);
							++nrContribs;
						}
					}
				}

				if (nEye == maxEyeDepth)
					break;

				SWCSpectrum f;
				if (!v.bsdf->SampleF(sw, v.wo, &v.wi, data[1], data[2],
					data[3], &f, &v.pdfR, BSDF_ALL, &v.flags, &v.pdf, true))
					break;

				// Check if the scattering is a passthrough event.
				if (v.flags != (BSDF_TRANSMISSION | BSDF_SPECULAR) ||
					!(v.bsdf->Pdf(sw, v.wo, v.wi, BxDFType(BSDF_TRANSMISSION | BSDF_SPECULAR)) > 0.f)) {
					vp.dAWeight = v.pdf * v.tPdf / vp.d2;
					if (!vp.bsdf->dgShading.scattered)
						vp.dAWeight *= vp.cosi;
					v.cosi = AbsDot(v.wi, v.bsdf->ng);
					v.rr = min(1.f, max(lightThreshold,
						f.Filter(sw) * v.coso / v.cosi));
					v.rrR = min(1.f, max(eyeThreshold, f.Filter(sw)));
					eyePath[nEye].flux = v.flux * f;
					if (nEye > rrStart) {
						if (v.rrR < data[0])
							break;
						eyePath[nEye].flux /= v.rrR;
					}
				} else {
					--nEye;
					v.flux *= f;
					vp.tPdfR *= v.pdfR;
					v.tPdf *= v.pdf;
					if (sampleIndex + 1 >= maxEyeDepth) {
						vp.rrR = 0.f;
						break;
					}
				}

				ray = Ray(v.p, v.wi);
				ray.time = sample.realTime;
				volume = v.bsdf->GetVolume(ray.d);
			}
			eyePathTraced = true;
			d = sqrtf(eye0.d2);
		}
	}

	// Light subpaths and connections to the eye subpath.
	for (u_int l = 0; l < pathSamplingCount; ++l) {
		sw.single = false; // Restore the single flag for a new sw context.

		float component = sample.sampler->GetOneD(sample, lightNumOffset, l);
		float lPdf;
		const Light *light = lightPathStrategy->SampleLight(scene, l,
			&component, &lPdf);
		if (!light)
			break;
		lPdf *= lightRayCount;
		const u_int lightGroup = light->group;
		const float directWeight = lightDirectStrategy->Pdf(scene, light) *
			shadowRayCount;
		for (u_int r = 0; r < lightRayCount; ++r) {
			component = sample.sampler->GetOneD(sample, lightPortalOffset,
				l * lightRayCount + r);
			float lightPos[2];
			sample.sampler->GetTwoD(sample, lightPosOffset,
				l * lightRayCount + r, lightPos);
			SWCSpectrum Le;

			if (maxLightDepth > 0 && light->SampleL(scene, sample,
				lightPos[0], lightPos[1], component, &lightPath[0].bsdf,
				&lightPath[0].dAWeight, &Le)) {
				BDPTVertex &light0(lightPath[0]);
				u_int nLight = 0;
				float lightDirectPdf = 0.f;
				light0.type = BDPTVertex::LIGHT_ORIGIN;
				light0.light = light;
				light0.lightGroup = lightGroup;
				light0.p = light0.bsdf->dgShading.p;
				light0.wi = Vector(light0.bsdf->dgShading.nn);
				light0.cosi = AbsDot(light0.wi, light0.bsdf->ng);
				light0.dAWeight *= lPdf;
				Le /= lPdf;
				light0.flux = SWCSpectrum(1.f);
				light0.tPdf = 1.f;
				light0.tPdfR = 1.f;
				light0.single = sw.single;
				if (light->IsDeltaLight()) {
					light0.delta = true;
					light0.dAWeight = -light0.dAWeight;
					light0.dARWeight = -fabsf(light0.dAWeight);
				} else {
					light0.dARWeight = fabsf(light0.dAWeight);
				}
				nLight = 1;

				// Connect eye subpath to the first light vertex (s=1).
				if (light0.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) != 0) {
					for (u_int j = 0; j < nEye; ++j) {
						BDPTVertex &vE(eyePath[j]);
						const float directPdf = light->Pdf(vE.p,
							light0.bsdf->dgShading) * directWeight;
						if (vE.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) == 0)
							continue;
						SWCSpectrum Ll(Le);
						float weight, d2;
						Vector ewi;
						bool single;
						if (Connect(scene, sample, eyePath, j + 1, lightPath,
							nLight, directPdf, false, &Ll, &weight, &d2,
							&ewi, single)) {
							if (j > 0) {
								partialContribution.Add(sw, Ll, lightGroup,
									weight, single);
								++nrContribs;
							} else if (EyeConnect(sample, vE.p, ewi,
								sqrtf(d2),
								PartialContribution::toXYZColor(sw, Ll, single),
								1.f, weight, lightBufferId, lightGroup))
								++nrContribs;
						}
					}
				}

				// Sample light subpath initial direction.
				const float *data = sample.sampler->GetLazyValues(sample,
					sampleLightOffsets[l * lightRayCount + r], 0);
				SWCSpectrum f0;
				if (maxLightDepth > 1 && light0.bsdf->SampleF(sw, light0.wi,
					&light0.wo, data[1], data[2], data[3], &f0, &light0.pdf,
					BSDF_ALL, &light0.flags, &light0.pdfR)) {
					light0.coso = AbsDot(light0.wo, light0.bsdf->ng);
					light0.rrR = min(1.f, max(eyeThreshold,
						f0.Filter(sw) * light0.cosi / light0.coso));
					light0.rr = min(1.f, max(lightThreshold, f0.Filter(sw)));
					Ray ray(light0.p, light0.wo);
					ray.time = sample.realTime;
					Intersection isect;
					lightPath[nLight].flux = light0.flux * f0;

					const Volume *volume = light0.bsdf->GetVolume(ray.d);
					bool scattered = light0.bsdf->dgShading.scattered;
					for (u_int sampleIndex = 1; sampleIndex < maxLightDepth; ++sampleIndex) {
						// Use sampleIndex so passthrough
						// materials don't overwrite values.
						lightPath[sampleIndex].tPdf = 1.f;
						lightPath[sampleIndex].tPdfR = 1.f;
						data = sample.sampler->GetLazyValues(sample,
							sampleLightOffsets[l * lightRayCount + r],
							sampleIndex);
						BDPTVertex &v = lightPath[nLight];
						BDPTVertex &vp = lightPath[nLight - 1];
						float spdf, spdfR;
						if (!scene.Intersect(sample, volume, scattered, ray,
							data[4], &isect, &v.bsdf, &spdf, &spdfR, &v.flux))
							break;
						scattered = v.bsdf->dgShading.scattered;

						v.type = BDPTVertex::SURFACE;
						v.wi = -ray.d;
						v.p = isect.dg.p;
						v.cosi = AbsDot(v.wi, v.bsdf->ng);
						v.tPdfR *= spdfR;
						v.flux /= spdf;
						v.single = sw.single;
						++nLight;

						vp.tPdf *= spdf;
						vp.d2 = DistanceSquared(vp.p, v.p);
						v.dAWeight = vp.pdf * vp.tPdf / vp.d2;
						if (!scattered)
							v.dAWeight *= v.cosi;
						// Compute light direct pdf between the first 2
						// vertices.
						if (nLight == 2)
							lightDirectPdf = light->Pdf(v.p,
								vp.bsdf->dgShading) * directWeight;

						// Connect eye subpath to this light vertex.
						if (v.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) != 0) {
							for (u_int j = 0; j < nEye; ++j) {
								BDPTVertex &vE(eyePath[j]);
								if (vE.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) == 0)
									continue;
								SWCSpectrum Ll(Le);
								float weight, d2;
								Vector ewi;
								bool single;
								if (Connect(scene, sample, eyePath, j + 1,
									lightPath, nLight, lightDirectPdf, false,
									&Ll, &weight, &d2, &ewi, single)) {
									if (j > 0) {
										partialContribution.Add(sw, Ll,
											lightGroup, weight, single);
										++nrContribs;
									} else if (EyeConnect(sample, vE.p,
										ewi, sqrtf(d2),
										PartialContribution::toXYZColor(sw,
										Ll, single), 1.f, weight,
										lightBufferId, lightGroup))
										++nrContribs;
								}
							}
						}

						if (nLight == maxLightDepth)
							break;

						SWCSpectrum f;
						if (!v.bsdf->SampleF(sw, v.wi, &v.wo, data[1],
							data[2], data[3], &f, &v.pdf, BSDF_ALL, &v.flags,
							&v.pdfR))
							break;

						if (v.flags != (BSDF_TRANSMISSION | BSDF_SPECULAR) ||
							!(v.bsdf->Pdf(sw, v.wi, v.wo, BxDFType(BSDF_TRANSMISSION | BSDF_SPECULAR)) > 0.f)) {
							vp.dARWeight = v.pdfR * v.tPdfR / vp.d2;
							if (!vp.bsdf->dgShading.scattered)
								vp.dARWeight *= vp.coso;
							v.coso = AbsDot(v.wo, v.bsdf->ng);
							v.rrR = min(1.f, max(eyeThreshold,
								f.Filter(sw) * v.cosi / v.coso));
							v.rr = min(1.f, max(lightThreshold, f.Filter(sw)));
							lightPath[nLight].flux = v.flux * f;
							if (nLight > rrStart) {
								if (v.rr < data[0])
									break;
								lightPath[nLight].flux /= v.rr;
							}
						} else {
							--nLight;
							v.flux *= f;
							vp.tPdf *= v.pdf;
							vp.tPdfR *= v.pdfR;
							if (sampleIndex + 1 >= maxLightDepth) {
								vp.rr = 0.f;
								break;
							}
						}

						ray = Ray(v.p, v.wo);
						ray.time = sample.realTime;
						volume = v.bsdf->GetVolume(ray.d);
					}
				}
			}
		}
	}

	if (eyePathTraced) {
		float xl, yl;
		if (!sample.camera->GetSamplePosition(eyePath[0].p, eyePath[0].wi,
			d, &xl, &yl))
			return nrContribs;
		partialContribution.Splat(sw, sample, xl, yl, d, alpha, eyeBufferId);
	}
	return nrContribs;
}

//------------------------------------------------------------------------------
// Integrator parsing code
//------------------------------------------------------------------------------

SurfaceIntegrator* BDPTIntegrator::CreateSurfaceIntegrator(const ParamSet &params)
{
	int eyeDepth = params.FindOneInt("eyedepth", 8);
	int lightDepth = params.FindOneInt("lightdepth", 8);
	float eyeThreshold = params.FindOneFloat("eyerrthreshold", 0.f);
	float lightThreshold = params.FindOneFloat("lightrrthreshold", 0.f);
	LightsSamplingStrategy *lds = LightsSamplingStrategy::Create("lightstrategy", params);
	int shadowRay = params.FindOneInt("shadowraycount", 1);
	int lightRay = params.FindOneInt("lightraycount", 1);
	LightsSamplingStrategy *lps = LightsSamplingStrategy::Create("lightpathstrategy", params);
	bool debug = params.FindOneBool("debug", false);

	return new BDPTIntegrator(max(eyeDepth, 0), max(lightDepth, 0),
		eyeThreshold, lightThreshold, lds, max(1, shadowRay),
		lps, max(1, lightRay), debug);
}

static DynamicLoader::RegisterSurfaceIntegrator<BDPTIntegrator> r("bdpt");
