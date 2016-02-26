//
// Traffic Sign Recognizer
// �㷨
// ���ߣ�SunnyCase
// �������ڣ�2016-02-26
#include "pch.h"
#include "algorithms.h"
using namespace concurrency;
using namespace concurrency::graphics;

DEFINE_NS_TSR_PRCSR

unorm Grayscale(const unorm_4 & color) restrict(cpu, amp)
{
	return unorm(color.r * 0.299f + color.g * 0.587f + color.b * 0.114f);
}

unorm Threshold(unorm color, float threshold) restrict(cpu, amp)
{
	return unorm(color < threshold ? 0.f : 1.f);
}

END_NS_TSR_PRCSR