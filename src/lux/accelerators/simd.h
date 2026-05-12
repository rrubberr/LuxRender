#pragma once
#include <cstddef>
#include <immintrin.h>
#include <stdint.h>
#include <math.h>
#include <xmmintrin.h>

template <size_t I, typename TI = uint8_t>
struct alignas(I*4) vfloat
{
	float f[I];

	vfloat() {};

	vfloat(float f_)
	{
		for(size_t i = 0; i < I; i++)
			f[i] = f_;
	};

	vfloat operator+(const vfloat &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] + a[i];
		return v;
	};

	vfloat operator-(const vfloat &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] - a[i];
		return v;
	};

	vfloat operator*(const vfloat &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] * a[i];
		return v;
	};

	vfloat operator/(const vfloat &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] / a[i];
		return v;
	};

	vfloat operator+(const float &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] + a;
		return v;
	};

	vfloat operator-(const float &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] - a;
		return v;
	};

	vfloat operator*(const float &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] * a;
		return v;
	};

	vfloat operator/(const float &a) const
	{
		vfloat v;
		for(size_t i = 0; i < I; i++)
			v[i] = f[i] / a;
		return v;
	};

	vfloat& operator+=(const vfloat &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] += a[i];
		return *this;
	};

	vfloat& operator-=(const vfloat &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] -= a[i];
		return *this;
	};

	vfloat& operator*=(const vfloat &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] *= a[i];
		return *this;
	};

	vfloat& operator/=(const vfloat &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] /= a[i];
		return *this;
	};

	vfloat& operator+=(const float &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] += a;
		return *this;
	};

	vfloat& operator-=(const float &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] -= a;
		return *this;
	};

	vfloat& operator*=(const float &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] *= a;
		return *this;
	};

	vfloat& operator/=(const float &a)
	{
		for(size_t i = 0; i < I; i++)
			f[i] /= a;
		return *this;
	};

	float& operator[](size_t i)
	{
		return f[i];
	};
	
	float operator[](size_t i) const
	{
		return f[i];
	};

	static size_t size()
	{
		return I;
	};

	template <typename T>
	T& as()
	{
		return *((T*)this);
	};
	
	template <typename T>
	T as() const
	{
		return *((T*)this);
	};
};

template <size_t I> 
inline vfloat<I> operator+(const float a, const vfloat<I> &b)
{
	vfloat<I> v;
	for(size_t i = 0; i < I; i++)
		v[i] = a + b[i];
	return v;
};

template <size_t I> 
inline vfloat<I> operator-(const float a, const vfloat<I> &b)
{
	vfloat<I> v;
	for(size_t i = 0; i < I; i++)
		v[i] = a - b[i];
	return v;
};

template <size_t I> 
inline vfloat<I> operator*(const float a, const vfloat<I> &b)
{
	vfloat<I> v;
	for(size_t i = 0; i < I; i++)
		v[i] = a * b[i];
	return v;
};

template <size_t I> 
inline vfloat<I> operator/(const float a, const vfloat<I> &b)
{
	vfloat<I> v;
	for(size_t i = 0; i < I; i++)
		v[i] = a / b[i];
	return v;
};

template <size_t I>
inline vfloat<I> vmin(const vfloat<I> &a, const vfloat<I> &b)
{
	vfloat<I> v;
	for(size_t i = 0; i < I; i++)
		v[i] = std::min<float>(a[i],b[i]);
	return v;
};

template <size_t I>
inline vfloat<I> vmax(const vfloat<I> &a, const vfloat<I> &b)
{
	vfloat<I> v;
	for(size_t i = 0; i < I; i++)
		v[i] = std::max<float>(a[i],b[i]);
	return v;
};

//128 bit wide
inline uint8_t vlt(const vfloat<4> &a, const vfloat<4> &b)
{
	return _mm_cmp_ps_mask(a.as<__m128>(),b.as<__m128>(),_CMP_LT_OS);
};

inline uint8_t vgt(const vfloat<4> &a, const vfloat<4> &b)
{
	return _mm_cmp_ps_mask(a.as<__m128>(),b.as<__m128>(),_CMP_GT_OS);
};

inline uint8_t vle(const vfloat<4> &a, const vfloat<4> &b)
{
	return _mm_cmp_ps_mask(a.as<__m128>(),b.as<__m128>(),_CMP_LE_OS);
};

inline uint8_t vge(const vfloat<4> &a, const vfloat<4> &b)
{
	return _mm_cmp_ps_mask(a.as<__m128>(),b.as<__m128>(),_CMP_GE_OS);
};

inline uint8_t veq(const vfloat<4> &a, const vfloat<4> &b)
{
	return _mm_cmp_ps_mask(a.as<__m128>(),b.as<__m128>(),_CMP_EQ_OQ);
};

inline uint8_t vneq(const vfloat<4> &a, const vfloat<4> &b)
{
	return _mm_cmp_ps_mask(a.as<__m128>(),b.as<__m128>(),_CMP_NEQ_OS);
};

//256 bit wide
inline uint8_t vlt(const vfloat<8> &a, const vfloat<8> &b)
{
	return _mm256_cmp_ps_mask(a.as<__m256>(),b.as<__m256>(),_CMP_LT_OS);
};

inline uint8_t vgt(const vfloat<8> &a, const vfloat<8> &b)
{
	return _mm256_cmp_ps_mask(a.as<__m256>(),b.as<__m256>(),_CMP_GT_OS);
};

inline uint8_t vle(const vfloat<8> &a, const vfloat<8> &b)
{
	return _mm256_cmp_ps_mask(a.as<__m256>(),b.as<__m256>(),_CMP_LE_OS);
};

inline uint8_t vge(const vfloat<8> &a, const vfloat<8> &b)
{
	return _mm256_cmp_ps_mask(a.as<__m256>(),b.as<__m256>(),_CMP_GE_OS);
};

inline uint8_t veq(const vfloat<8> &a, const vfloat<8> &b)
{
	return _mm256_cmp_ps_mask(a.as<__m256>(),b.as<__m256>(),_CMP_EQ_OQ);
};

inline uint8_t vneq(const vfloat<8> &a, const vfloat<8> &b)
{
	return _mm256_cmp_ps_mask(a.as<__m256>(),b.as<__m256>(),_CMP_NEQ_OS);
};

//512 bit wide
inline uint16_t vlt(const vfloat<16> &a, const vfloat<16> &b)
{
	return _mm512_cmp_ps_mask(a.as<__m512>(),b.as<__m512>(),_CMP_LT_OS);
};

inline uint16_t vgt(const vfloat<16> &a, const vfloat<16> &b)
{
	return _mm512_cmp_ps_mask(a.as<__m512>(),b.as<__m512>(),_CMP_GT_OS);
};

inline uint16_t vle(const vfloat<16> &a, const vfloat<16> &b)
{
	return _mm512_cmp_ps_mask(a.as<__m512>(),b.as<__m512>(),_CMP_LE_OS);
};

inline uint16_t vge(const vfloat<16> &a, const vfloat<16> &b)
{
	return _mm512_cmp_ps_mask(a.as<__m512>(),b.as<__m512>(),_CMP_GE_OS);
};

inline uint16_t veq(const vfloat<16> &a, const vfloat<16> &b)
{
	return _mm512_cmp_ps_mask(a.as<__m512>(),b.as<__m512>(),_CMP_EQ_OQ);
};

inline uint16_t vneq(const vfloat<16> &a, const vfloat<16> &b)
{
	return _mm512_cmp_ps_mask(a.as<__m512>(),b.as<__m512>(),_CMP_NEQ_OS);
};