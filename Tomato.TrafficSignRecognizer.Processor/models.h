//
// Traffic Sign Recognizer
// 模型
// 作者：SunnyCase
// 创建日期：2016-03-18
#include "common.h"
#include <xstddef>
#include <amp_graphics.h>
#pragma once

DEFINE_NS_TSR_PRCSR

struct EllipseParam
{
	double A, B, C, D, E;
	double x, y;
	double a, b;
	double theta;
	uint32_t id;
	uint32_t rank;
	uint32_t area;
	uint32_t length;
	float lambda;
};

struct EllipsePoints
{
	concurrency::graphics::float_2 p1, p2, p3;
};

struct UnitCirclePoint
{
	double rho;
	double theta;
};

struct ZernikeDataSource
{
	uint32_t pointsCount;
	concurrency::array<UnitCirclePoint, 1> points;
	float area;

	ZernikeDataSource(uint32_t pointsCount, concurrency::array<UnitCirclePoint, 1>&& points, float area)
		:pointsCount(pointsCount), points(std::move(points)), area(area)
	{

	}
};

public value struct ZernikeResult
{
	uint32_t p;
	int q;
	float z;
};

END_NS_TSR_PRCSR

namespace std
{
	template<>
	struct equal_to<NS_TSR_PRCSR::ZernikeResult>
	{
		_CONST_FUN bool operator()(const NS_TSR_PRCSR::ZernikeResult& _Left, const NS_TSR_PRCSR::ZernikeResult& _Right) const
		{	
			return _Left.p == _Right.p && _Left.q == _Right.q && _Left.z == _Right.z;
		}
	};
}