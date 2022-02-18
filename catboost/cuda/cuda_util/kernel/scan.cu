#include "scan.cuh"
#include "segmented_scan_helpers.cuh"
#include "fill.cuh"

#include <library/cpp/cuda/wrappers/cub_include.h>
#include _CUB_INCLUDE(cub/device/device_scan.cuh)

namespace NKernel {

    template <typename T, typename TOut>
    cudaError_t ScanVector(const T* input, TOut* output, ui32 size, bool inclusive, TScanKernelContext<T, TOut>& context, TCudaStream stream) {
        using TKernelContext = TScanKernelContext<T, TOut>;

        if (inclusive) {
            return cub::DeviceScan::InclusiveSum(context.PartResults, context.NumParts, input, output, size, stream);
        } else {
            return cub::DeviceScan::ExclusiveSum(context.PartResults, context.NumParts, input, output, size, stream);
        }
    }

    template <class T>
    struct TToSignedConversion {
        using TSignedType = T;
    };


    template <>
    struct TToSignedConversion<ui32> {
        using TSignedType = int;
    };

    template <typename T_, typename TOut_>
    cudaError_t SegmentedScanNonNegativeVector(const T_* input, TOut_* output, ui32 size, bool inclusive, TScanKernelContext<T_, TOut_>& context, TCudaStream stream) {
        using TKernelContext = TScanKernelContext<T_, TOut_>;
        using T = typename TToSignedConversion<T_>::TSignedType;
        using TOut = typename TToSignedConversion<TOut_>::TSignedType;
        T zeroValue = 0.0f;
        if (inclusive) {
            return cub::DeviceScan::InclusiveScan((TOut*)context.PartResults.Get(), context.NumParts, (const T*)input, (TOut*)output, TNonNegativeSegmentedSum(), size, stream);
        } else {
            return cub::DeviceScan::ExclusiveScan((TOut*)context.PartResults.Get(), context.NumParts, (const T*) input, (TOut*)output, TNonNegativeSegmentedSum(), zeroValue, size, stream);
        }
    }


    template <typename T_>
    cudaError_t SegmentedScanAndScatterNonNegativeVector(const T_* input, const ui32* indices, T_* output,
                                                         ui32 size, bool inclusive,
                                                         TScanKernelContext<T_, T_>& context,
                                                         TCudaStream stream) {
        using TKernelContext = TScanKernelContext<T_, T_>;
        using T = typename TToSignedConversion<T_>::TSignedType;

        if (inclusive) {
            TNonNegativeSegmentedScanOutputIterator<cub::STORE_CS, T,  ptrdiff_t, true>  outputIterator((T*)output, indices, indices + size);
            return cub::DeviceScan::InclusiveScan((T*)context.PartResults.Get(), context.NumParts, (const T*)input, outputIterator, TNonNegativeSegmentedSum(), size, stream);
        } else {
            TNonNegativeSegmentedScanOutputIterator<cub::STORE_CS, T,  ptrdiff_t, false>  outputIterator((T*)output, indices, indices + size);
            FillBuffer<T>((T*)output, 0, size, stream);
            return cub::DeviceScan::InclusiveScan((T*)context.PartResults.Get(), context.NumParts, (const T*) input, outputIterator, TNonNegativeSegmentedSum(), size, stream);
        }
    }

    template <class T, class TOut>
    ui64 ScanVectorTempSize(ui32 size, bool inclusive) {
        ui64 sizeInBytes = 0;
        if (inclusive) {
            cub::DeviceScan::InclusiveSum<const T*, TOut*>(nullptr, sizeInBytes, nullptr, nullptr, size);
        } else {
            cub::DeviceScan::ExclusiveSum<const T*, TOut*>(nullptr, sizeInBytes, nullptr, nullptr, size);
        }
        return sizeInBytes;
    }



    #define SCAN_VECTOR(Type, TypeOut) \
    template  cudaError_t ScanVector<Type, TypeOut>(const Type *input, TypeOut *output, ui32 size, bool inclusive, TScanKernelContext<Type, TypeOut>& context, TCudaStream stream); \
    template  cudaError_t SegmentedScanNonNegativeVector<Type>(const Type *input, TypeOut *output, ui32 size, bool inclusive, TScanKernelContext<Type, TypeOut>& context, TCudaStream stream); \
    template ui64 ScanVectorTempSize<Type, TypeOut>(ui32, bool);

    SCAN_VECTOR(int, int)
    SCAN_VECTOR(ui32, ui32)
    SCAN_VECTOR(float, float)
    SCAN_VECTOR(double, double)
    SCAN_VECTOR(ui32, ui64)

    #define SEGMENTED_SCAN_VECTOR(Type) \
    template  cudaError_t SegmentedScanAndScatterNonNegativeVector<Type>(const Type *input, const ui32* indices, Type *output, ui32 size, bool inclusive, TScanKernelContext<Type, Type>& context, TCudaStream stream);

    SEGMENTED_SCAN_VECTOR(int)
    SEGMENTED_SCAN_VECTOR(ui32)
    SEGMENTED_SCAN_VECTOR(float)
    SEGMENTED_SCAN_VECTOR(double)


}
