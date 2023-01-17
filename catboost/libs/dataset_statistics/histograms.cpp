#include "histograms.h"

#include <util/generic/algorithm.h>
#include <util/generic/size_literals.h>
#include <util/generic/ymath.h>

#include <cmath>

using namespace NCB;

constexpr auto floatInf = std::numeric_limits<float>::infinity();


void TFloatFeatureHistogram::Update(TFloatFeatureHistogram& histograms) {
    Nans += histograms.Nans;
    MinusInf += histograms.MinusInf;
    PlusInf += histograms.PlusInf;
    if (Borders.HistogramType == histograms.Borders.HistogramType && Borders.HistogramType == EHistogramType::Exact) {
        for (auto const &imap: histograms.Borders.BitHistogram) {
            Borders.BitHistogram[imap.first] += imap.second;
        }
        ConvertBitToUniformIfNeeded();
    } else {
        ConvertBitToUniform();
        histograms.ConvertBitToUniform();
        CB_ENSURE_INTERNAL(Borders == histograms.Borders, "Different borders");
        if (!histograms.Histogram.empty()) {
            if (Histogram.empty()) {
                Histogram.resize(histograms.Histogram.size(), 0);
            }
            for (size_t idx = 0; idx < Histogram.size(); ++idx) {
                Histogram[idx] += histograms.Histogram[idx];
            }
        }
    }
}

void TFloatFeatureHistogram::CalcUniformHistogram(
    const TVector<float>& features,
    const TVector<ui64>& count) {
    CB_ENSURE(count.empty() || count.size() == features.size());
    if (Borders.HistogramType == EHistogramType::Exact) {
        for (auto feature: features) {
            if (ProcessNotNummeric(feature)) {
                continue;
            }
            Borders.BitHistogram[float(feature)]++;
        }
        ConvertBitToUniformIfNeeded();
        return;
    }

    Borders.CheckHistogramType(EHistogramType::Uniform);
    Histogram.resize(Borders.MaxBorderCount + 1);

    double length = Borders.MaxValue - Borders.MinValue;
    double step = length / double(Borders.MaxBorderCount + 1);

    for (ui32 idx = 0; idx < features.size(); ++idx) {
        float value = features[idx];
        ui32 inc = count.empty() ? 1 : count[idx];
        if (ProcessNotNummeric(value)) {
            continue;
        }
        if (value <= Borders.MinValue) {
            Histogram[0] += inc;
            continue;
        }
        auto index = ui32(double(value - Borders.MinValue) / step);
        if (index >= Histogram.size()) {
            index = Histogram.size() - 1;
        }
        Histogram[index] += inc;
    }
}

bool TFloatFeatureHistogram::ProcessNotNummeric(float value) {
    if (std::isnan(value)) {
        ++Nans;
        return true;
    }
    if (std::isinf(value)) {
        if (value == floatInf) {
            ++PlusInf;
        } else {
            ++MinusInf;
        }
        return true;
    }
    return false;
}

void TFloatFeatureHistogram::CalcHistogramWithBorders(
    const TVector<float>& featureColumnPtr
) {
    Borders.CheckHistogramType(EHistogramType::Borders);
    TVector<float> featureColumnCopy(featureColumnPtr);
    CalcHistogramWithBorders(&featureColumnCopy);
}

void TFloatFeatureHistogram::CalcHistogramWithBorders(
    TVector<float>* featureColumnPtr
) {
    Borders.CheckHistogramType(EHistogramType::Borders);
    TVector<float>& features = *featureColumnPtr;
    Histogram.resize(Borders.Size());
    std::sort(features.begin(), features.end());
    ui32 borderIndex = 0;
    for (auto value: features) {
        if (ProcessNotNummeric(value)) {
            continue;
        }
        while (borderIndex + 1 < Histogram.size() && value > Borders.Borders[borderIndex]) {
            ++borderIndex;
        }
        Histogram[borderIndex]++;
    }
}

NJson::TJsonValue TBorders::ToJson() const {
    NJson::TJsonValue result;
    InsertEnumType("HistogramType", HistogramType, &result);
    switch (HistogramType) {
        case EHistogramType::Uniform:
            result.InsertValue("MaxBorderCount", MaxBorderCount);
            result.InsertValue("MinValue", MinValue);
            result.InsertValue("MaxValue", MaxValue);
            break;
        case EHistogramType::Exact:
            result.InsertValue("Bins", VectorToJson(GetBins()));
            result.InsertValue("Hist", VectorToJson(GetExactHistogram()));
            result.InsertValue("MinValue", MinValue);
            result.InsertValue("MaxValue", MaxValue);
            break;
        case EHistogramType::Borders:
            result.InsertValue("Borders", VectorToJson(GetBorders()));
            break;
        default:
            Y_ASSERT(false);
    }
    return result;
}

void TFloatFeatureHistogram::ConvertBitToUniformIfNeeded() {
    if (Borders.BitHistogram.size() < MAX_EXACT_HIST_SIZE) {
        return;
    }
    ConvertBitToUniform();
};

void TFloatFeatureHistogram::ConvertBitToUniform() {
    if (Borders.HistogramType != EHistogramType::Exact) {
        return;
    }
    auto features = Borders.GetBins();
    auto count = Borders.GetExactHistogram();
    Borders.HistogramType = EHistogramType::Uniform;
    CalcUniformHistogram(features, count);
    Borders.BitHistogram.clear();
}

TVector<ui64> TFloatFeatureHistogram::GetHistogram() const {
    if (Borders.HistogramType == EHistogramType::Exact) {
        return Borders.GetExactHistogram();
    }
    return Histogram;
}

NJson::TJsonValue TFloatFeatureHistogram::ToJson() const {
    NJson::TJsonValue result;

    result.InsertValue("Nans", Nans);
    result.InsertValue("MinusInf", MinusInf);
    result.InsertValue("PlusInf", PlusInf);
    result.InsertValue("HistogramSize", Borders.Size());
    result.InsertValue("Borders", Borders.ToJson());
    auto histogram = GetHistogram();
    TVector<NJson::TJsonValue> histogramJson;
    for (const auto& item : histogram) {
        histogramJson.emplace_back(item);
    }
    result.InsertValue("Histogram", VectorToJson(histogramJson));
    return result;
}

NJson::TJsonValue THistograms::ToJson() const {
    NJson::TJsonValue result;
    TVector<NJson::TJsonValue> histogram;
    for (const auto& item : FloatFeatureHistogram) {
        histogram.push_back(item.ToJson());
    }
    result.InsertValue("FloatFeatureHistogram", VectorToJson(histogram));
    if (TargetHistogram.Defined()) {
        histogram.clear();
        for (const auto& item : *TargetHistogram) {
            histogram.push_back(item.ToJson());
        }
        result.InsertValue("TargetHistogram", VectorToJson(histogram));
    }
    return result;
}

void THistograms::Update(THistograms& histograms) {
    Y_ASSERT(FloatFeatureHistogram.size() == histograms.FloatFeatureHistogram.size());
    for (size_t idx = 0; idx < FloatFeatureHistogram.size(); ++idx) {
        FloatFeatureHistogram[idx].Update(histograms.FloatFeatureHistogram[idx]);
    }
    CB_ENSURE(TargetHistogram.Defined() == histograms.TargetHistogram.Defined());
    if (TargetHistogram.Defined()) {
        for (size_t idx = 0; idx < TargetHistogram->size(); ++idx) {
            TargetHistogram->at(idx).Update(histograms.TargetHistogram->at(idx));
        }
    }
}

void THistograms::AddTargetHistogram(
    ui32 targetId,
    TVector<float>* features
) {
    CB_ENSURE(TargetHistogram.Defined());
    CB_ENSURE_INTERNAL(
        targetId < TargetHistogram->size(),
        "TargetId " << targetId << " is bigger then TargetHistogram size " << TargetHistogram->size()
    );
    TargetHistogram->at(targetId).CalcUniformHistogram(*features);
}

void THistograms::AddFloatFeatureUniformHistogram(
    ui32 featureId,
    TVector<float>* features
) {
    CB_ENSURE_INTERNAL(
        featureId < FloatFeatureHistogram.size(),
        "FeaturedId " << featureId << " is bigger then FloatFeatureHistogram size " << FloatFeatureHistogram.size()
    );
    FloatFeatureHistogram[featureId].CalcUniformHistogram(*features);
}

bool TBorders::operator==(const TBorders& rhs) {
    if (HistogramType != rhs.HistogramType) {
        return false;
    }
    switch (HistogramType) {
        case EHistogramType::Uniform:
            return MaxBorderCount == rhs.MaxBorderCount && MinValue == rhs.MinValue && MaxValue == rhs.MaxValue;
        case EHistogramType::Borders:
            return EqualBorders(rhs.Borders);
        case EHistogramType::Exact:
            return BitHistogram == rhs.BitHistogram;
        default:
            Y_ASSERT(false);
    }
    return false;
}

ui32 TBorders::Size() const {
    switch (HistogramType) {
        case EHistogramType::Uniform:
            return MaxBorderCount;
        case EHistogramType::Borders:
            return Borders.size();
        case EHistogramType::Exact:
            return BitHistogram.size();
        default:
            Y_ASSERT(false);
    }
    return 0;
}

static TVector<float> GetUniformBorders(
    ui32 maxBorderCount,
    float minValue,
    float maxValue
) {
    if (maxBorderCount == 0) {
        return {};
    }
    CB_ENSURE(minValue < maxValue, "Min value > Max value: " << minValue << ">" << maxValue);
    TVector<float> borders(maxBorderCount + 2);
    borders[0] = minValue;
    borders[maxBorderCount + 1] = maxValue;
    double step = (maxValue - minValue) / double(maxBorderCount + 1);
    for (ui32 idx = 1; idx <= maxBorderCount; ++idx) {
        borders[idx] = minValue + float(step * double(idx));
    }
    return borders;
}

TVector<float> TBorders::GetBorders() const {
    switch (HistogramType) {
        case EHistogramType::Uniform:
            return GetUniformBorders(MaxBorderCount, MinValue, MaxValue);
        case EHistogramType::Borders:
            return Borders;
        case EHistogramType::Exact:
            CB_ENSURE(false, "Not supported");
        default:
            Y_ASSERT(false);
    }
    return {};
}

TVector<float> TBorders::GetBins() const {
    Y_ASSERT(HistogramType == EHistogramType::Exact);
    TVector<float> borders;
    for (auto const &imap: BitHistogram) {
        borders.push_back(imap.first);
    }
    return borders;
}

TVector<ui64> TBorders::GetExactHistogram() const {
    Y_ASSERT(HistogramType == EHistogramType::Exact);
    TVector<ui64> histogram;
    for (auto const &imap: BitHistogram) {
        histogram.push_back(imap.second);
    }
    return histogram;
}
