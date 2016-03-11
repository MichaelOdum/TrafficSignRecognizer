﻿//
// Traffic Sign Recognizer
// Recognizer
// 作者：SunnyCase
// 创建日期：2016-02-25
#include "pch.h"
#include "Recognizer.h"
#include <amp_math.h>
#include "algorithms.h"
#include "amprng/amp_sobol_rng.h"

using namespace NS_TSR_PRCSR;
using namespace Platform;
using namespace concurrency;
using namespace concurrency::graphics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace WRL;

Recognizer::Recognizer(unsigned int width, unsigned int height)
	:_targetImageExtent(height, width),
	_acc_view(concurrency::direct3d::create_accelerator_view(accelerator(), true)),
	_targetImage(_targetImageExtent, 16U, _acc_view), _outputTex(_targetImageExtent, _acc_view),
	_edgeTex(_targetImageExtent, 8, _acc_view), _tangentTex(_targetImageExtent, 32, _acc_view),
	_redTex(_targetImageExtent, 8, _acc_view)
{

}

Windows::Foundation::IAsyncAction ^ Recognizer::SetTarget(BitmapFrame ^ frame)
{
	return create_async([=]
	{
		return PrepareTargetImage(frame);
	});
}

IAsyncOperation<bool>^ Recognizer::Recognize(Windows::Storage::Streams::IRandomAccessStream^ outputStream)
{
	return create_async([=]() -> task<bool>
	{
		return FindContours().then([=]
		{
			return create_task(BitmapEncoder::CreateAsync(BitmapEncoder::JpegEncoderId, outputStream))
				.then([=](BitmapEncoder^ encoder)
			{
				auto data = std::make_shared<std::vector<uint>>(_targetImageExtent.size());
				return copy_async(_outputTex, data->begin()).to_task().then([=]
				{
					encoder->SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, _targetImageExtent[1], _targetImageExtent[0],
						96., 96., ArrayReference<byte>((byte*)data->data(), data->size() * 4));
					return create_task(encoder->FlushAsync());
				});
			});
		}).then([]
		{
			return false;
		});
	});
}

concurrency::task<void> Recognizer::PrepareTargetImage(BitmapFrame ^ frame)
{
	auto transform = ref new BitmapTransform();
	return create_task(frame->GetPixelDataAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, transform,
		ExifOrientationMode::RespectExifOrientation, ColorManagementMode::ColorManageToSRgb))
		.then([=](PixelDataProvider^ dataProvider)
	{
		auto data = dataProvider->DetachPixelData();
		array<uint, 2> src(_targetImageExtent, (uint*)data->begin(), (uint*)data->end(), _acc_view);
		texture_view<unorm_4, 2> targetImage(_targetImage);
		parallel_for_each(_acc_view, src.extent, [&src, targetImage](index<2> index) restrict(amp)
		{
			auto pixel = src[index];

			auto r = ((pixel & 0xFF0000) >> 16) / 255.f;
			auto g = ((pixel & 0xFF00) >> 8) / 255.f;
			auto b = (pixel & 0xFF) / 255.f;
			targetImage.set(index, unorm_4(r, g, b, 1.f));
		});
	});
}

concurrency::task<void> Recognizer::FindContours()
{
	//FindEdgesAndTangent();
	//texture_view<const unorm, 2> edges(_edgeTex);
	//texture_view<const unorm_4, 2> input(_targetImage);

	AbsorbRedTexels();
	FindEdgesAndTangent();

	texture_view<const unorm, 2> redTex(_redTex);
	texture_view<const unorm, 2> edges(_edgeTex);
	texture_view<const float, 2> tangents(_tangentTex);
	array_view<uint, 2> outputTex(_outputTex);

	return FindEllipses().then([=]
	{
		parallel_for_each(_acc_view, _targetImageExtent, [outputTex, tangents, edges](index<2> index) restrict(amp)
		{
			const auto gray = fast_math::sin(fast_math::atan(tangents[index]));
			if (gray < 0)
			{
				auto lastGray = uint(-gray * 255);
				outputTex[index] = uint(0xFF0000FF | (lastGray << 16));
			}
			else if (gray > 0)
			{
				auto lastGray = uint(gray * 255);
				outputTex[index] = uint(0xFF0000FF | (lastGray << 8));
			}
			//const auto r = input[index].r * 255.f;
			//const auto g = input[index].g * 255.f;
			//const auto b = input[index].b * 255.f;
			//if (((g - r) / r >= 1.f) && (g / b >= 1.f) && (g - b <= 40))
			//{
			//	outputTex[index] = uint(0xFF000000);
			//}
			//else
			//{
			//	if (r - g > 40 && r - b > 50)
			//	{
			//		outputTex[index] = uint(0xFFFF0000);
			//	}
			//	else
			//		outputTex[index] = uint(0xFF000000);
			//}
		});
	});
}

void Recognizer::FindEdgesAndTangent()
{
	uint radius = 3;
	texture_view<unorm, 2> edgeView(_edgeTex);
	texture_view<float, 2> tangentView(_tangentTex);
	texture_view<const unorm, 2> redTex(_redTex);
	texture_view<const unorm_4, 2> inputTex(_targetImage);
	parallel_for_each(_acc_view, redTex.extent, [redTex, edgeView, inputTex, tangentView, radius](index<2> index) restrict(amp)
	{
		auto edge = SusanTest(redTex, index, radius, 0.1f);
		edgeView.set(index, edge);

		if (edge > 0.f)
			tangentView.set(index, CalculateTangent(redTex, index));
	});
}

void Recognizer::AbsorbRedTexels()
{
	texture_view<const unorm_4, 2> input(_targetImage);
	texture_view<unorm, 2> redTex(_redTex);

	parallel_for_each(_acc_view, _targetImageExtent, [input, redTex](index<2> index) restrict(amp)
	{
		const float_2 p(index[1] + 0.5f, index[0] + 0.5f);
		const float_2 points[] = {
			p + float_2(-1, -1),
			p + float_2(+0, -1),
			p + float_2(+1, -1),

			p + float_2(-1, +0),
			p + float_2(+0, +0),
			p + float_2(+1, +0),

			p + float_2(-1, +1),
			p + float_2(+0, +1),
			p + float_2(+1, +1)
		};
		uint32_t hasCount = 0;
		for (auto&& point : points)
		{
			if (IsRed(input.sample<filter_point>(coord(point, input.extent))))
				if (++hasCount >= 5)
				{
					redTex.set(index, unorm(1.f));
					break;
				}
		}
	});

	texture_view<const unorm, 2> redReader(_redTex);
	texture<unorm, 2> tmpTex(_targetImageExtent, 8, _acc_view);
	texture_view<unorm, 2> tmpWriter(tmpTex);
	parallel_for_each(_acc_view, _targetImageExtent, [tmpWriter, redReader](index<2> index) restrict(amp)
	{
		const float_2 p(index[1] + 0.5f, index[0] + 0.5f);
		if (redReader.sample<filter_point>(coord(p, redReader.extent)) == 0.f)
		{
			if (redReader.sample<filter_point>(coord(p + float_2(-1, +0), redReader.extent)) != 0.f ||
				redReader.sample<filter_point>(coord(p + float_2(+1, +0), redReader.extent)) != 0.f)
				tmpWriter.set(index, unorm(1.f));
			else if (redReader.sample<filter_point>(coord(p + float_2(+0, -1), redReader.extent)) != 0.f ||
				redReader.sample<filter_point>(coord(p + float_2(+0, +1), redReader.extent)) != 0.f)
				tmpWriter.set(index, unorm(1.f));
		}
		else
			tmpWriter.set(index, unorm(1.f));
	});

	copy(tmpTex, _redTex);
}

#define F 1000.f

EllipseParam Test(float height)
{
	// 取 3 点为中心边长为 3 的 3 个正方形，共 27 点
	const float_2 points[] = {
		float_2(54, height - 75), float_2(53, height - 75),float_2(55, height - 75),
		float_2(101, height - 50), float_2(101, height - 49),float_2(101, height - 51),
		float_2(89, height - 68), float_2(90, height - 68),float_2(89, height - 69),float_2(88, height - 69),
		float_2(97, height - 61), float_2(97, height - 60),float_2(98, height - 60),float_2(97, height - 61),float_2(96, height - 61),
		float_2(75, height - 75), float_2(74, height - 75),float_2(76, height - 74),
	};
	static const graphics::uint maxPoints = sizeof(points) / sizeof(float_2);

	uint32_t rows = 0;
	float U[maxPoints][5];
	float UT[5][maxPoints];
	for (uint32_t i = 0; i < maxPoints; i++)
	{
		const auto point = points[i];
		if (1)
		{
			// x² xy y² x y
			U[rows][0] = point.x * point.x;
			U[rows][1] = point.x * point.y;
			U[rows][2] = point.y * point.y;
			U[rows][3] = point.x;
			U[rows][4] = point.y;

			UT[0][rows] = U[rows][0];
			UT[1][rows] = U[rows][1];
			UT[2][rows] = U[rows][2];
			UT[3][rows] = U[rows][3];
			UT[4][rows] = U[rows][4];
			rows++;
		}
	}

	// UT x U 形成 5 x 5 矩阵
	// UT x V 形成 5 x 1 矩阵
	float mat[5][5 + 1];
	for (uint32_t i = 0; i < 5; i++)
	{
		for (uint32_t j = 0; j < 5; j++)
		{
			float sum = 0;
			for (uint32_t k = 0; k < rows; k++)
				sum += UT[i][k] * U[k][j];
			mat[i][j] = sum;
		}
		float sum = 0;
		for (uint32_t k = 0; k < rows; k++)
			sum += UT[i][k] * -F;
		mat[i][5] = sum;
	}
	if (auto solved = Solve(mat))
	{
		EllipseParam ellipse{ mat[0][5], mat[1][5], mat[2][5], mat[3][5], mat[4][5] };
		// 椭圆判别式 B² - 4AC < 0
		auto value = ellipse.B * ellipse.B - 4.f * ellipse.A * ellipse.C;
		if (value < 0.f)
		{
			ellipse.y = height - -(ellipse.E - ellipse.B * ellipse.D / (2 * ellipse.A))
				/ (2 * ellipse.C - ellipse.B * ellipse.B / (2 * ellipse.A));
			ellipse.x = -(ellipse.B * ellipse.y + ellipse.D) / (2 * ellipse.A);
			ellipse.a = fast_math::sqrt((fast_math::pow((ellipse.E - ellipse.B * ellipse.D / (2 * ellipse.A)), 2)
				/ (4 * ellipse.C - ellipse.B * ellipse.B / ellipse.A) - F + ellipse.D *ellipse.D / (4 * ellipse.A)) / ellipse.A);
			ellipse.b = fast_math::sqrt((fast_math::pow((ellipse.E - ellipse.B * ellipse.D / (2 * ellipse.A)), 2)
				/ (4 * ellipse.C - ellipse.B * ellipse.B / ellipse.A) - F + ellipse.D *ellipse.D / (4 * ellipse.A))
				/ (ellipse.C - ellipse.B * ellipse.B / (4 * ellipse.A)));

			return ellipse;
		}
	}
}

task<void> Recognizer::FindEllipses()
{
	float height = _targetImageExtent[0];
	//auto el = Test(height);

	//array_view<uint, 2> outputTex(_outputTex);
	//parallel_for_each(_acc_view, _targetImageExtent, [=](index<2> index) restrict(amp)
	//{
	//	auto gray = OnEllipse(el, float_2(index[1], height - index[0]), 0.5f) ? 1.f : 0.f;
	//	auto lastGray = uint(gray * 255);
	//	if (lastGray)
	//		outputTex[index] = uint(0xFF000000 | (lastGray << 16) | (lastGray << 8) | lastGray);
	//});

	array<uint, 2> edgePointsCount(1, 1, _acc_view, access_type_read);
	array<index<2>, 2> edgePositions(_targetImageExtent, _acc_view);
	texture_view<const unorm, 2> edgeView(_edgeTex);

	parallel_for_each(_acc_view, edgeView.extent, [&edgePointsCount, &edgePositions, edgeView](index<2> index) restrict(amp)
	{
		if (edgeView[index] != 0.f)
		{
			auto idx = atomic_fetch_add(&edgePointsCount(0, 0), 1);
			auto x = idx % edgeView.extent[1];
			auto y = idx / edgeView.extent[1];
			edgePositions(y, x) = index;
		}
	});

	const auto edgeSize = extent<1>(edgePointsCount(0, 0));
	const auto maxEllipses = edgeSize / 2;

	typedef sobol_rng<1>::sobol_number<float> sobol_float_number;
	sobol_rng_collection<sobol_rng<1>, 1> sc_rng(_acc_view, extent<1>(500), 1);
	concurrency::graphics::texture_view<const float, 2> tangentView(_tangentTex);
	array<uint32_t, 1> fitsCount(1, _acc_view, access_type_read);
	array<EllipsePoints, 1> points(maxEllipses, _acc_view, access_type_read);
	array<EllipseParam, 1> ellipses(maxEllipses, _acc_view);

	parallel_for_each(_acc_view, maxEllipses, [=, &edgePositions, &fitsCount, &points, &ellipses](index<1> index) restrict(amp)
	{
		const auto id1 = index[0];
		const auto x1 = id1 % edgeView.extent[1];
		const auto y1 = id1 / edgeView.extent[1];
		const auto p1Index = edgePositions(y1, x1);

		// Get the sobol RNG 
		auto rng = sc_rng[index];
		// Skip ahead to the right position
		rng.skip(sc_rng.direction_numbers(), index[0]);

		const auto id2 = int(rng.get_single(1) * edgeSize[0]);
		const auto x2 = id2 % edgeView.extent[1];
		const auto y2 = id2 / edgeView.extent[1];
		const auto p2Index = edgePositions(y2, x2);
		FindEllipsePoints(p1Index, p2Index, tangentView[p1Index], tangentView[p2Index], edgeView, tangentView, fitsCount, points);
		//FitEllipse(p1Index, p2Index, tangentView[p1Index], tangentView[p2Index], edgeView, tangentView, fitsCount, ellipses);
	});
	const auto ellipseCount = fitsCount(0);
	auto p = points.data();
	p = nullptr;

	//parallel_for_each(_acc_view, edgeSize, [=, &fitsCount, &ellipses, &edgePositions](index<1> index)restrict(amp)
	//{
	//	const auto id1 = index[0];
	//	const auto x1 = id1 % edgeView.extent[1];
	//	const auto y1 = id1 / edgeView.extent[1];
	//	const auto p1 = edgePositions(y1, x1);

	//	const auto ellipseCount = fitsCount(0);
	//	for (uint32_t i = 0; i < ellipseCount; i++)
	//	{
	//		auto& ellipse = ellipses(i);
	//		if (OnEllipse(ellipse, float_2(p1[1], height - p1[0]), .5f))
	//			atomic_fetch_add(&ellipse.rank, 1);
	//	}
	//});

	//if (ellipseCount)
	//{
	//	std::vector<EllipseParam> ellipsesSort(ellipseCount);
	//	copy(ellipses.section(extent<1>(ellipseCount)), ellipsesSort.begin());
	//	std::sort(ellipsesSort.begin(), ellipsesSort.end(), [](const EllipseParam& left, const EllipseParam& right)
	//	{
	//		return left.rank > right.rank;
	//	});
	//	std::vector<EllipseParam> ellipsesFit;
	//	const float lambda = 0.5f;
	//	for (auto&& it : ellipsesSort)
	//	{
	//		auto min = lambda * 3.14f * (1.5f * (it.a + it.b) - fast_math::sqrt(it.a * it.b));
	//		if (it.rank >= min)
	//			ellipsesFit.emplace_back(it);
	//	}

	//	for (auto&& el : ellipsesSort)
	//	{
	//		array_view<uint, 2> outputTex(_outputTex);
	//		parallel_for_each(_acc_view, _targetImageExtent, [=](index<2> index) restrict(amp)
	//		{
	//			auto gray = OnEllipse(el, float_2(index[1], height - index[0]), 0.5f) ? 1.f : 0.f;
	//			auto lastGray = uint(gray * 255);
	//			if (lastGray)
	//				outputTex[index] = uint(0xFF000000 | (lastGray << 16) | (lastGray << 8) | lastGray);
	//		});
	//	}
	//}

	//float mat[5][6] = 
	//{
	//	{ -7, 6,    3,  5.2, -7, 13.2 * -7 + -76.5 * 6 + 83.125 * 3 + 9763.2 * 5.2 + -8352.7 * -7 },
	//	{ 13, -2.7, 6,  0.3, 0, 13.2 * 13 + -76.5 * -2.7 + 83.125 * 6 + 9763.2 * 0.3 + -8352.7 * 0 },
	//	{ 8,  -1.7, 0,  6.9, 3, 13.2 * 8 + -76.5 * -1.7 + 83.125 * 0 + 9763.2 * 6.9 + -8352.7 * 3 },
	//	{ 88, 7,    3,  -1,  -2.5, 13.2 * 88 + -76.5 * 7 + 83.125 * 3 + 9763.2 * -1 + -8352.7 * -2.5 },
	//	{ 0,  10,   -9, -5.2, 8.2, 13.2 * 0 + -76.5 * 10 + 83.125 * -9 + 9763.2 * -5.2 + -8352.7 * 8.2 }
	//};
	//auto ret = Solve(mat);

	return task_from_result();
}
