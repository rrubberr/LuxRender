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
// (reflectant and illuminant), and CIE luminance.
//
// Monochromatic SWA: a lux2 lane carries ONE wavelength. The scalar reference
// carries 4 (SWCSpectrum_<4>). We drive both to the SAME single wavelength --
// the scalar's SampleSingle() slot -- and compare lux2's single value against
// the scalar's c[single_w] (and Y). Every packet lane carries the same
// broadcast input, so lane 0 is compared against the scalar result.

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

lux2::RGBColorP ToP(const RGBColor &rgb) {
	return lux2::RGBColorP(lux2::FloatP(rgb.c[0]), lux2::FloatP(rgb.c[1]),
		lux2::FloatP(rgb.c[2]));
}

// Scalar reference at a single wavelength: sample the scalar's 4 wavelengths,
// split to a single slot (SampleSingle), and return that wavelength together
// with the reflectant/illuminant SPD value c[single_w] and luminance Y there.
// The caller drives lux2 to the SAME wavelength via FromWavelength.
struct ScalarSingle {
	float wl;
	float value;
	float Y;
};
ScalarSingle ScalarRefSingle(float u1, const RGBColor &rgb, bool illum) {
	SpectrumWavelengths sw;
	sw.Sample(u1);
	const float wl = sw.SampleSingle(); // sets sw.single, returns w[single_w]
	SWCSpectrum s = illum ? SWCSpectrum(sw, RGBIllumSPD(rgb))
	                      : SWCSpectrum(sw, RGBReflSPD(rgb));
	return ScalarSingle{wl, s.c[sw.single_w], s.Y(sw)};
}

// lux2's uniform wavelength must land in [START, END) and its bins must match
// the scalar's bin computation at the same wavelength.
void CheckWavelengths() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();

		SpectrumWavelengths sw;
		sw.Sample(u1);
		const float wl = sw.SampleSingle();

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(wl));

		// The wavelength round-trips exactly and stays in range.
		ok = ok && Close(First(swp.w), wl, 1e-5f);
		ok = ok && First(swp.w) >= 380.f && First(swp.w) <= 720.f;
		// The CIE bin must match the scalar's at the single slot.
		const int j = int(sw.single_w);
		ok = ok && (swp.binXYZ[0] == sw.binsXYZ[j]);
	}
	Check(ok, "wavelength sampling");
}

// Smits RGB->SPD (reflectant) at a single wavelength must match c[single_w].
void CheckRefl() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		const ScalarSingle ref = ScalarRefSingle(u1, rgb, false);

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(ref.wl));
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, false);

		if (!Close(First(s), ref.value)) {
			std::cerr << "  [dbg t=" << t << " rgb=(" << rgb.c[0] << ","
				<< rgb.c[1] << "," << rgb.c[2] << ") w=" << ref.wl
				<< " got=" << First(s) << " ref=" << ref.value << "]\n";
		}
		ok = ok && Close(First(s), ref.value);
	}
	Check(ok, "Smits reflectant");
}

// Smits RGB->SPD (illuminant) at a single wavelength must match c[single_w].
void CheckIllum() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		const ScalarSingle ref = ScalarRefSingle(u1, rgb, true);

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(ref.wl));
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, true);

		ok = ok && Close(First(s), ref.value);
	}
	Check(ok, "Smits illuminant");
}

// CIE luminance at a single wavelength must match SWCSpectrum::Y (single).
void CheckLuminance() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		const ScalarSingle ref = ScalarRefSingle(u1, rgb, false);

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(ref.wl));
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, false);
		ok = ok && Close(First(lux2::SWCY(s, swp)), ref.Y);
	}
	Check(ok, "CIE luminance");
}

// ConstantColorTexture must evaluate to the same single-wavelength value.
void CheckConstantTexture() {
	const RGBColor rgb(0.7f, 0.3f, 0.1f);
	const float u1 = 0.42f;

	const ScalarSingle ref = ScalarRefSingle(u1, rgb, false);

	lux2::ConstantColorTexture tex(ToP(rgb));
	lux2::SpectrumWavelengthsP swp;
	swp.FromWavelength(lux2::FloatP(ref.wl));
	lux2::SWCSpectrumP s = tex.Evaluate(lux2::DifferentialGeometryP(), swp);

	bool ok = tex.IsConstant() && Close(First(s), ref.value);
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
