/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

// Cross-check the SoA SPD/CIE pipeline (lux2::color) against the scalar
// luxrays reference for wavelength sampling, Smits RGB->SPD reconstruction
// (reflectant and illuminant), and CIE luminance. Every packet lane carries
// the same broadcast input, so lane 0 is compared against the scalar result.

#include "core/color.h"
#include "core/texture.h"

#include "luxrays/core/color/swcspectrum.h"
#include "luxrays/core/color/spectrumwavelengths.h"
#include "luxrays/core/color/spds/rgbrefl.h"
#include "luxrays/core/color/spds/rgbillum.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using luxrays::RGBColor;
using luxrays::RGBReflSPD;
using luxrays::RGBIllumSPD;
using luxrays::SpectrumWavelengths;
using luxrays::SWCSpectrum;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
	if (!cond) {
		std::cerr << "  FAIL: " << what << std::endl;
		++g_failures;
	} else {
		std::cout << "  ok:   " << what << std::endl;
	}
}

bool Close(float a, float b, float tol = 2e-4f) {
	return std::fabs(a - b) <= tol * (1.f + std::fabs(a) + std::fabs(b));
}

float RandUnit() { return float(std::rand()) / float(RAND_MAX); }

// Read lane 0 of a broadcast packet.
float First(const lux2::FloatP &p) {
	float buf[lux2::PACKET_WIDTH];
	enoki::store_unaligned(buf, p);
	return buf[0];
}

// Sample a packet of wavelengths from a broadcast u1.
void SampleSW(lux2::SpectrumWavelengthsP &sw, float u1) {
	sw.Sample(lux2::FloatP(u1));
}

lux2::RGBColorP ToP(const RGBColor &rgb) {
	return lux2::RGBColorP(lux2::FloatP(rgb.c[0]), lux2::FloatP(rgb.c[1]),
		lux2::FloatP(rgb.c[2]));
}

// Scalar reference: sample wavelengths, then evaluate an RGB color as a
// reflectant or illuminant SPD at those wavelengths.
void ScalarRef(float u1, const RGBColor &rgb, bool illum,
               float out[WAVELENGTH_SAMPLES], float *outY) {
	SpectrumWavelengths sw;
	sw.Sample(u1);
	SWCSpectrum s = illum ? SWCSpectrum(sw, RGBIllumSPD(rgb))
	                      : SWCSpectrum(sw, RGBReflSPD(rgb));
	for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
		out[i] = s.c[i];
	if (outY)
		*outY = s.Y(sw);
}

// Wavelength sampling must match the scalar stratified sampler.
void CheckWavelengths() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();

		SpectrumWavelengths sw;
		sw.Sample(u1);

		lux2::SpectrumWavelengthsP swp;
		SampleSW(swp, u1);

		for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
			ok = ok && Close(First(swp.w[i]), sw.w[i], 1e-5f);
	}
	Check(ok, "wavelength sampling");
}

// Smits RGB->SPD (reflectant) must match SWCSpectrum(sw, RGBReflSPD).
void CheckRefl() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		float ref[WAVELENGTH_SAMPLES];
		ScalarRef(u1, rgb, false, ref, nullptr);

		lux2::SpectrumWavelengthsP swp;
		SampleSW(swp, u1);
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, false);

		for (int i = 0; i < WAVELENGTH_SAMPLES; ++i) {
			if (!Close(First(s[i]), ref[i])) {
				std::cerr << "  [dbg t=" << t << " i=" << i
					<< " rgb=(" << rgb.c[0] << "," << rgb.c[1] << ","
					<< rgb.c[2] << ") w=" << First(swp.w[i])
					<< " got=" << First(s[i]) << " ref=" << ref[i] << "]\n";
			}
			ok = ok && Close(First(s[i]), ref[i]);
		}
	}
	Check(ok, "Smits reflectant");
}

// Smits RGB->SPD (illuminant) must match SWCSpectrum(sw, RGBIllumSPD).
void CheckIllum() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		float ref[WAVELENGTH_SAMPLES];
		ScalarRef(u1, rgb, true, ref, nullptr);

		lux2::SpectrumWavelengthsP swp;
		SampleSW(swp, u1);
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, true);

		for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
			ok = ok && Close(First(s[i]), ref[i]);
	}
	Check(ok, "Smits illuminant");
}

// CIE luminance must match SWCSpectrum::Y.
void CheckLuminance() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		float ref[WAVELENGTH_SAMPLES], refY = 0.f;
		ScalarRef(u1, rgb, false, ref, &refY);

		lux2::SpectrumWavelengthsP swp;
		SampleSW(swp, u1);
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, false);
		ok = ok && Close(First(lux2::SWCY(s, swp)), refY);
	}
	Check(ok, "CIE luminance");
}

// ConstantColorTexture must evaluate to the same spectrum as the scalar path.
void CheckConstantTexture() {
	const RGBColor rgb(0.7f, 0.3f, 0.1f);
	const float u1 = 0.42f;

	float ref[WAVELENGTH_SAMPLES];
	ScalarRef(u1, rgb, false, ref, nullptr);

	lux2::ConstantColorTexture tex(ToP(rgb));
	lux2::SpectrumWavelengthsP swp;
	SampleSW(swp, u1);
	lux2::SWCSpectrumP s = tex.Evaluate(lux2::DifferentialGeometryP(), swp);

	bool ok = tex.IsConstant();
	for (int i = 0; i < WAVELENGTH_SAMPLES && ok; ++i)
		ok = ok && Close(First(s[i]), ref[i]);
	Check(ok, "ConstantColorTexture matches scalar");
}

} // namespace

int main() {
	std::srand(20260921);
	CheckWavelengths();
	CheckRefl();
	CheckIllum();
	CheckLuminance();
	CheckConstantTexture();

	if (g_failures == 0) {
		std::cout << "lux2colorcheck: ALL CHECKS PASSED" << std::endl;
		return 0;
	}
	std::cerr << "lux2colorcheck: " << g_failures << " CHECK(S) FAILED"
		<< std::endl;
	return 1;
}
