//
// Traffic Sign Recognizer
// �㷨
// ���ߣ�SunnyCase
// �������ڣ�2016-02-26
#include "common.h"
#include <amp.h>
#include <amp_graphics.h>

DEFINE_NS_TSR_PRCSR

concurrency::graphics::unorm Grayscale(const concurrency::graphics::unorm_4& color) restrict(cpu, amp);
concurrency::graphics::unorm Threshold(concurrency::graphics::unorm color, float threshold) restrict(cpu, amp);

END_NS_TSR_PRCSR