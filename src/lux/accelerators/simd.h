#pragma once
#include <cstddef>
#include <stdint.h>
#include <math.h>

template <size_t I>
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