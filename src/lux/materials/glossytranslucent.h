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
 
// Modified GlossyTranslucent material based on glossy2 by paco

#include "lux.h"
#include "material.h"

namespace lux
{

// GlossyTranslucent Class Declarations
class GlossyTranslucent : public Material {
public:
	// GlossyTranslucent Public Methods
	GlossyTranslucent(std::shared_ptr<Texture<SWCSpectrum> > &kd,
		std::shared_ptr<Texture<SWCSpectrum> > &kt,
		std::shared_ptr<Texture<SWCSpectrum> > &ks,
		std::shared_ptr<Texture<SWCSpectrum> > &ks2,
		std::shared_ptr<Texture<SWCSpectrum> > &ka,
		std::shared_ptr<Texture<SWCSpectrum> > &ka2,
		std::shared_ptr<Texture<float> > &i,
		std::shared_ptr<Texture<float> > &i2,
		std::shared_ptr<Texture<float> > &d,
		std::shared_ptr<Texture<float> > &d2,
		std::shared_ptr<Texture<float> > &u,
		std::shared_ptr<Texture<float> > &u2,
		std::shared_ptr<Texture<float> > &v,
		std::shared_ptr<Texture<float> > &v2,
		bool mb,
		bool mb2,
		const ParamSet &mp) : Material("GlossyTranslucent-" + boost::lexical_cast<string>(this), mp),
		Kd(kd), Kt(kt),
		Ks(ks), Ks_bf(ks2), Ka(ka), Ka_bf(ka2), depth(d), depth_bf(d2),
		index(i), index_bf(i2), nu(u), nu_bf(u2), nv(v), nv_bf(v2),
		multibounce(mb), multibounce_bf(mb2) { }
	virtual ~GlossyTranslucent() { }
	virtual BSDF *GetBSDF(luxrays::MemoryArena &arena, const SpectrumWavelengths &sw,
		const Intersection &isect,
		const DifferentialGeometry &dgShading) const;
	
	Texture<SWCSpectrum> *GetKdTexture() { return Kd.get(); }
	Texture<SWCSpectrum> *GetKtTexture() { return Kt.get(); }
	Texture<SWCSpectrum> *GetKsTexture() { return Ks.get(); }
	Texture<SWCSpectrum> *GetKaTexture() { return Ka.get(); }
	Texture<float> *GetNuTexture() { return nu.get(); }
	Texture<float> *GetNvTexture() { return nv.get(); }
	Texture<float> *GetDepthTexture() { return depth.get(); }
	Texture<float> *GetIndexTexture() { return index.get(); }
	bool IsMultiBounce() const { return multibounce; }
	Texture<SWCSpectrum> *GetKsBfTexture() { return Ks_bf.get(); }
	Texture<SWCSpectrum> *GetKaBfTexture() { return Ka_bf.get(); }
	Texture<float> *GetNuBfTexture() { return nu_bf.get(); }
	Texture<float> *GetNvBfTexture() { return nv_bf.get(); }
	Texture<float> *GetDepthBfTexture() { return depth_bf.get(); }
	Texture<float> *GetIndexBfTexture() { return index_bf.get(); }
	bool IsMultiBounceBf() const { return multibounce_bf; }

	static Material * CreateMaterial(const Transform &xform,
		const ParamSet &mp);

private:
	// GlossyTranslucent Private Data
	std::shared_ptr<Texture<SWCSpectrum> > Kd, Kt, Ks, Ks_bf, Ka, Ka_bf;
	std::shared_ptr<Texture<float> > depth, depth_bf, index, index_bf;
	std::shared_ptr<Texture<float> > nu, nu_bf, nv, nv_bf;
	bool multibounce, multibounce_bf;
};

}//namespace lux
