//
// Traffic Sign Recognizer
// ģ��ѵ����
// ���ߣ�SunnyCase
// �������ڣ�2016-05-5
#include "common.h"
#include "FeatureExtractor.h"
#include "SignModel.h"
#pragma once

DEFINE_NS_TSR_PRCSR

public ref class ModelTrainer sealed
{
public:
	ModelTrainer();

	void AddSamples(int className, Windows::Foundation::Collections::IVectorView<Windows::Storage::StorageFile^>^ samples);
	Windows::Foundation::IAsyncOperation<SignModel^>^ Train();
private:
	concurrency::task<SignModel^> TrainIntern();
private:
	struct ClassSamples
	{
		int className;
		Windows::Foundation::Collections::IVectorView<Windows::Storage::StorageFile^>^ samples;

		ClassSamples(int className, Windows::Foundation::Collections::IVectorView<Windows::Storage::StorageFile^>^ samples)
			:className(className), samples(samples)
		{}
	};

	std::vector<ClassSamples> _samples;
};

END_NS_TSR_PRCSR