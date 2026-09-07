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

#ifndef LUX_OSFUNC_H
#define LUX_OSFUNC_H

#include <boost/cstdint.hpp>
using boost::int32_t;
using boost::uint32_t;
#include <istream>
#include <ostream>

#include <stddef.h>
#include <sys/time.h>

#include <boost/version.hpp>
#include <boost/interprocess/detail/atomic.hpp>
#include <atomic>

#if (BOOST_VERSION < 104800)
using boost::interprocess::detail::atomic_cas32;
using boost::interprocess::detail::atomic_inc32;
using boost::interprocess::detail::atomic_read32;
using boost::interprocess::detail::atomic_write32;
using boost::interprocess::detail::atomic_add32;
#else
using boost::interprocess::ipcdetail::atomic_cas32;
using boost::interprocess::ipcdetail::atomic_inc32;
using boost::interprocess::ipcdetail::atomic_read32;
using boost::interprocess::ipcdetail::atomic_write32;
using boost::interprocess::ipcdetail::atomic_add32;
#endif // BOOST_VERSION >= 104800


namespace lux
{

// Dade - used to check and swap bytes in the network rendering code and
// other places
extern bool osIsLittleEndian();
extern void osWriteLittleEndianFloat(bool isLittleEndian,
		std::basic_ostream<char> &os, float value);
extern float osReadLittleEndianFloat(bool isLittleEndian,
		std::basic_istream<char> &is);
extern void osWriteLittleEndianDouble(bool isLittleEndian,
		std::basic_ostream<char> &os, double value);
extern double osReadLittleEndianDouble(bool isLittleEndian,
		std::basic_istream<char> &is);
extern void osWriteLittleEndianInt(bool isLittleEndian,
		std::basic_ostream<char> &os, int32_t value);
extern int32_t osReadLittleEndianInt(bool isLittleEndian,
		std::basic_istream<char> &is);
extern void osWriteLittleEndianUInt(bool isLittleEndian,
		std::basic_ostream<char> &os, uint32_t value);
extern uint32_t osReadLittleEndianUInt(bool isLittleEndian,
		std::basic_istream<char> &is);

inline double osWallClockTime() {
	struct timeval t;
	gettimeofday(&t, NULL);

	return t.tv_sec + t.tv_usec / 1000000.0;
}

//------------------------------------------------------------------------------
// Atomic ops
//------------------------------------------------------------------------------

inline void osAtomicAdd(float *val, const float delta) {
#ifdef LUX_USE_TBB
	union bits {
		float f;
		uint32_t i;
	};

	std::atomic<uint32_t> *a = reinterpret_cast<std::atomic<uint32_t>*>(val);
	bits oldVal, newVal;
	oldVal.i = a->load(std::memory_order_seq_cst);
	do {
		newVal.f = oldVal.f + delta;
	} while (!a->compare_exchange_weak(oldVal.i, newVal.i,
				std::memory_order_seq_cst, std::memory_order_seq_cst));
#else
	union bits {
		float f;
		uint32_t i;
	};

	bits oldVal, newVal;

	do {
#if (defined(__i386__) || defined(__amd64__))
		__asm__ __volatile__("pause\n");
#endif

		oldVal.f = *val;
		newVal.f = oldVal.f + delta;
	} while (atomic_cas32(reinterpret_cast<uint32_t*>(val), newVal.i, oldVal.i) != oldVal.i);
#endif
}

inline void osAtomicAdd(unsigned int *val, const unsigned int delta) {
#ifdef LUX_USE_TBB
	reinterpret_cast<std::atomic<uint32_t>*>(val)
		->fetch_add((uint32_t)delta, std::memory_order_seq_cst);
#else
	atomic_add32(((uint32_t *)val), (uint32_t)delta);
#endif
}

/**
 * Atomically increments a 32bit variable
 * @return Previous value, before increment
 */
inline unsigned int osAtomicInc(unsigned int *val) {
#ifdef LUX_USE_TBB
	return reinterpret_cast<std::atomic<uint32_t>*>(val)
		->fetch_add(1u, std::memory_order_seq_cst);
#else
	return atomic_inc32(reinterpret_cast<uint32_t*>(val));
#endif
}

/**
 * Atomically reads a 32bit variable
 * @return Value read
 */
inline unsigned int osAtomicRead(unsigned int *val) {
#ifdef LUX_USE_TBB
	return reinterpret_cast<std::atomic<uint32_t>*>(val)
		->load(std::memory_order_seq_cst);
#else
	return atomic_read32(reinterpret_cast<uint32_t*>(val));
#endif
}

/**
 * Atomically writes a 32bit variable
 */
inline void osAtomicWrite(unsigned int *val, unsigned int newVal) {
#ifdef LUX_USE_TBB
	reinterpret_cast<std::atomic<uint32_t>*>(val)
		->store((uint32_t)newVal, std::memory_order_seq_cst);
#else
	atomic_write32(reinterpret_cast<uint32_t*>(val), static_cast<uint32_t>(newVal));
#endif
}

/**
 * Atomic compare-and-swap on a 32bit value.
 * Sets *p = desired if *p == comparand.
 * @return The value that was in *p BEFORE the operation.
 */
inline uint32_t osAtomicCas32(volatile uint32_t *p, uint32_t desired,
		uint32_t comparand) {
#ifdef LUX_USE_TBB
	std::atomic<uint32_t> *a =
			reinterpret_cast<std::atomic<uint32_t>*>(const_cast<uint32_t*>(p));
	a->compare_exchange_strong(comparand, desired,
			std::memory_order_seq_cst, std::memory_order_seq_cst);
	return comparand;
#else
	return atomic_cas32(p, desired, comparand);
#endif
}

// Floating point exception debuging
// Currently only works on linux
// You can use disable/enable at anypoint on your code, if DEBUGFP is defined,
// it may slow stuff a lot, but without DEBUGFP, it will not change anything.

namespace fpdebug
{

//#define DEBUGFP

#if defined(DEBUGFP)
void disable();
void enable();

#else
inline void disable(){}
inline void enable(){}
#endif

} // namespace fpdebug

}//namespace lux

#endif // LUX_OSFUNC_H
