//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "HalPolicy.hpp"

#include "../1.0/HalPolicy.hpp"

namespace armnn_driver
{
namespace hal_1_1
{

bool HalPolicy::ConvertOperation(const Operation& operation, const Model& model, ConversionData& data)
{
    if (compliantWithV1_0(operation))
    {
        hal_1_0::HalPolicy::Operation v10Operation = convertToV1_0(operation);
        hal_1_0::HalPolicy::Model v10Model = convertToV1_0(model);

        return hal_1_0::HalPolicy::ConvertOperation(v10Operation, v10Model, data);
    }
    else
    {
        switch (operation.type)
        {
            case V1_1::OperationType::DIV:
                return ConvertDiv(operation, model, data);
            case V1_1::OperationType::SUB:
                return ConvertSub(operation, model, data);
            case V1_1::OperationType::MEAN:
                return ConvertMean(operation, model, data);
            case V1_1::OperationType::PAD:
                return ConvertPad(operation, model, data);
            case V1_1::OperationType::SPACE_TO_BATCH_ND:
                return ConvertSpaceToBatchNd(operation, model, data);
            case V1_1::OperationType::SQUEEZE:
                return ConvertSqueeze(operation, model, data);
            case V1_1::OperationType::STRIDED_SLICE:
                return ConvertStridedSlice(operation, model, data);
            case V1_1::OperationType::TRANSPOSE:
                return ConvertTranspose(operation, model, data);
            case V1_1::OperationType::BATCH_TO_SPACE_ND:
                return ConvertBatchToSpaceNd(operation, model, data);
            default:
                return Fail("%s: Operation type %s not supported in ArmnnDriver",
                            __func__, toString(operation.type).c_str());
        }
    }
}

bool HalPolicy::ConvertDiv(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input0 = ConvertToLayerInputHandle(operation, 0, model, data);
    LayerInputHandle input1 = ConvertToLayerInputHandle(operation, 1, model, data);

    if (!input0.IsValid() || !input1.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    // The FuseActivation parameter is always the input index 2
    // and it should be optional
    ActivationFn activationFunction;
    if (!GetOptionalInputActivation(operation, 2, activationFunction, model, data))
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const Operand* outputOperand = GetOutputOperand(operation, 0, model);
    if (!outputOperand)
    {
        return false;
    }

    const armnn::TensorInfo& outInfo = GetTensorInfoForOperand(*outputOperand);

    if (!IsLayerSupported(__func__,
                          armnn::IsDivisionSupported,
                          data.m_Compute,
                          input0.GetTensorInfo(),
                          input1.GetTensorInfo(),
                          outInfo))
    {
        return false;
    }

    armnn::IConnectableLayer* const startLayer = data.m_Network->AddDivisionLayer();
    armnn::IConnectableLayer* const endLayer = ProcessActivation(outInfo, activationFunction, startLayer, data);

    const armnn::TensorInfo& inputTensorInfo0 = input0.GetTensorInfo();
    const armnn::TensorInfo& inputTensorInfo1 = input1.GetTensorInfo();

    if (endLayer)
    {
        BroadcastTensor(input0, input1, startLayer, *data.m_Network);
        return SetupAndTrackLayerOutputSlot(operation, 0, *endLayer, model, data);
    }

    return Fail("%s: ProcessActivation failed", __func__);
}

bool HalPolicy::ConvertSub(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input0 = ConvertToLayerInputHandle(operation, 0, model, data);
    LayerInputHandle input1 = ConvertToLayerInputHandle(operation, 1, model, data);

    if (!input0.IsValid() || !input1.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    // The FuseActivation parameter is always the input index 2
    // and it should be optional
    ActivationFn activationFunction;
    if (!GetOptionalInputActivation(operation, 2, activationFunction, model, data))
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const Operand* outputOperand = GetOutputOperand(operation, 0, model);
    if (!outputOperand)
    {
        return false;
    }

    const armnn::TensorInfo& outInfo = GetTensorInfoForOperand(*outputOperand);

    if (!IsLayerSupported(__func__,
                          armnn::IsSubtractionSupported,
                          data.m_Compute,
                          input0.GetTensorInfo(),
                          input1.GetTensorInfo(),
                          outInfo))
    {
        return false;
    }

    armnn::IConnectableLayer* const startLayer = data.m_Network->AddSubtractionLayer();
    armnn::IConnectableLayer* const endLayer = ProcessActivation(outInfo, activationFunction, startLayer, data);

    const armnn::TensorInfo& inputTensorInfo0 = input0.GetTensorInfo();
    const armnn::TensorInfo& inputTensorInfo1 = input1.GetTensorInfo();

    if (endLayer)
    {
        BroadcastTensor(input0, input1, startLayer, *data.m_Network);
        return SetupAndTrackLayerOutputSlot(operation, 0, *endLayer, model, data);
    }

    return Fail("%s: ProcessActivation failed", __func__);
}

bool HalPolicy::ConvertMean(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input = ConvertToLayerInputHandle(operation, 0, model, data);
    if (!input.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const Operand* axisOperand = GetInputOperand(operation, 1, model);
    if (!axisOperand)
    {
        return Fail("%s: Could not read input 1", __func__);
    }

    std::vector<int32_t> axis;
    if (!GetTensorInt32Values(*axisOperand, axis, model, data))
    {
        return Fail("%s: Input 1 has invalid values", __func__);
    }

    const armnn::TensorInfo& inputInfo = input.GetTensorInfo();

    // Convert the axis to unsigned int and remove duplicates.
    unsigned int rank = inputInfo.GetNumDimensions();
    std::set<unsigned int> uniqueAxis;
    std::transform(axis.begin(), axis.end(),
                   std::inserter(uniqueAxis, uniqueAxis.begin()),
                   [rank](int i) -> unsigned int { return (i + rank) % rank; });

    // Get the "keep dims" flag.
    int32_t keepDims = 0;
    if (!GetInputInt32(operation, 2, keepDims, model, data))
    {
        return Fail("%s: Could not read input 2", __func__);
    }

    armnn::MeanDescriptor descriptor;
    descriptor.m_Axis.assign(uniqueAxis.begin(), uniqueAxis.end());
    descriptor.m_KeepDims = keepDims > 0;

    const Operand* output = GetOutputOperand(operation, 0, model);
    if (!output)
    {
        return Fail("%s: Could not read output 0", __func__);
    }

    const armnn::TensorInfo& outputInfo = GetTensorInfoForOperand(*output);

    if (!IsLayerSupported(__func__,
                          armnn::IsMeanSupported,
                          data.m_Compute,
                          inputInfo,
                          outputInfo,
                          descriptor))
    {
        return false;
    }

    armnn::IConnectableLayer* const layer = data.m_Network->AddMeanLayer(descriptor);
    assert(layer != nullptr);
    input.Connect(layer->GetInputSlot(0));

    return SetupAndTrackLayerOutputSlot(operation, 0, *layer, model, data);
}

bool HalPolicy::ConvertPad(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input = ConvertToLayerInputHandle(operation, 0, model, data);

    if (!input.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const armnn::TensorInfo& inputInfo  = input.GetTensorInfo();

    const Operand* paddingsOperand = GetInputOperand(operation, 1, model);

    if (!paddingsOperand)
    {
        return Fail("%s: Could not read paddings operand", __func__);
    }

    unsigned int rank = inputInfo.GetNumDimensions();
    armnn::TensorShape paddingsOperandShape = GetTensorShapeForOperand(*paddingsOperand);
    if (paddingsOperandShape.GetNumDimensions() != rank || paddingsOperandShape.GetNumElements() != 2)
    {
        return Fail("%s: Operation has invalid paddings operand: expected shape [%d, 2]",  __func__, rank);
    }

    std::vector<int32_t> paddings;
    GetTensorInt32Values(*paddingsOperand, paddings, model, data);

    // add padding for each dimension of input tensor.
    armnn::PadDescriptor descriptor;
    for (unsigned int i = 0; i < paddings.size() - 1; i += 2)
    {
        int paddingBeforeInput = paddings[i];
        int paddingAfterInput = paddings[i + 1];
        if (paddingBeforeInput < 0 || paddingAfterInput < 0)
        {
            return Fail("%s: Operation has invalid paddings operand, invalid padding values.",  __func__);
        }
        descriptor.m_PadList.emplace_back((unsigned int) paddingBeforeInput, (unsigned int) paddingAfterInput);
    }

    const Operand* output = GetOutputOperand(operation, 0, model);
    if (!output)
    {
        return Fail("%s: Could not read output 0", __func__);
    }

    const armnn::TensorInfo& outputInfo = GetTensorInfoForOperand(*output);

    if (!IsLayerSupported(__func__,
                          armnn::IsPadSupported,
                          data.m_Compute,
                          inputInfo,
                          outputInfo,
                          descriptor))
    {
        return false;
    }

    armnn::IConnectableLayer* const layer = data.m_Network->AddPadLayer(descriptor);
    assert(layer != nullptr);
    input.Connect(layer->GetInputSlot(0));
    layer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    return SetupAndTrackLayerOutputSlot(operation, 0, *layer, model, data);
}

bool HalPolicy::ConvertSpaceToBatchNd(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input = ConvertToLayerInputHandle(operation, 0, model, data);

    if (!input.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const armnn::TensorInfo& inputInfo = input.GetTensorInfo();
    unsigned int rank = inputInfo.GetNumDimensions();
    unsigned int spatialDim = rank - 2;

    if (rank != 4)
    {
        Fail("%s: Only inputs with rank 4 are supported", __func__);
    }

    armnn::SpaceToBatchNdDescriptor descriptor;
    descriptor.m_DataLayout = armnn::DataLayout::NHWC;

    const Operand* blockShapeOperand = GetInputOperand(operation, 1, model);
    const Operand* paddingsOperand = GetInputOperand(operation, 2, model);

    armnn::TensorShape blockShapeOperandShape = GetTensorShapeForOperand(*blockShapeOperand);
    if (blockShapeOperandShape.GetNumDimensions() != 1 || blockShapeOperandShape.GetNumElements() != spatialDim)
    {
        return Fail("%s: Operation has invalid block shape operand: expected shape [%d]", __func__, spatialDim);
    }

    std::vector<int32_t> blockShape;
    GetTensorInt32Values(*blockShapeOperand, blockShape, model, data);
    for (unsigned int i = 0; i < blockShape.size(); i++)
    {
        if (blockShape[i] < 1)
        {
            return Fail("%s: Block shape must be at least 1 in all dimensions.", __func__);
        }

        descriptor.m_BlockShape.push_back((unsigned int) blockShape[i]);
    }

    armnn::TensorShape paddingsOperandShape = GetTensorShapeForOperand(*paddingsOperand);
    if (paddingsOperandShape.GetNumDimensions() != 2 || paddingsOperandShape.GetNumElements() != 2 * spatialDim)
    {
        return Fail("%s: Operation has invalid paddings operand: expected shape [%d, 2]", __func__, spatialDim);
    }

    std::vector<int32_t> paddings;
    GetTensorInt32Values(*paddingsOperand, paddings, model, data);
    for (unsigned int i = 0; i < paddings.size() - 1; i += 2)
    {
        int paddingBeforeInput = paddings[i];
        int paddingAfterInput = paddings[i + 1];
        if (paddingBeforeInput < 0 || paddingAfterInput < 0)
        {
            return Fail("%s: Operation has invalid paddings operand, invalid padding values.", __func__);
        }

        descriptor.m_PadList.emplace_back((unsigned int) paddingBeforeInput, (unsigned int) paddingAfterInput);
    }

    const Operand* output = GetOutputOperand(operation, 0, model);
    if (!output)
    {
        return Fail("%s: Could not read output 0", __func__);
    }

    const armnn::TensorInfo& outputInfo = GetTensorInfoForOperand(*output);
    if (!IsLayerSupported(__func__,
                          armnn::IsSpaceToBatchNdSupported,
                          data.m_Compute,
                          inputInfo,
                          outputInfo,
                          descriptor))
    {
        return false;
    }

    armnn::IConnectableLayer* const layer = data.m_Network->AddSpaceToBatchNdLayer(descriptor);
    assert(layer != nullptr);
    input.Connect(layer->GetInputSlot(0));

    return SetupAndTrackLayerOutputSlot(operation, 0, *layer, model, data);
}

bool HalPolicy::ConvertSqueeze(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input = ConvertToLayerInputHandle(operation, 0, model, data);

    if (!input.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const armnn::TensorInfo& inputInfo  = input.GetTensorInfo();

    unsigned int rank = inputInfo.GetNumDimensions();
    if (rank > 4)
    {
        Fail("%s: Inputs with rank greater than 4 are not supported", __func__);
    }

    // NOTE: Axis is an optional parameter to SQUEEZE, therefore we do not want to generate a failure
    // if the operand index is out of bounds.
    const Operand* axisOperand = GetInputOperand(operation, 1, model, false);

    const uint32_t dimensionSequence[] = { 0, 1, 2, 3 };

    std::vector<int32_t> axis;
    if (!axisOperand)
    {
        axis.assign(dimensionSequence,
                    dimensionSequence + rank);
    }
    else
    {
        GetTensorInt32Values(*axisOperand, axis, model, data);
    }


    std::vector<uint32_t> outputDims;
    for (unsigned int i = 0; i < rank; i++)
    {
        bool skipSqueeze = (std::find(axis.begin(), axis.end(), i) == axis.end());
        auto currentDimension = inputInfo.GetShape()[i];
        if (skipSqueeze || currentDimension != 1)
        {
            outputDims.push_back(currentDimension);
        }
    }

    armnn::TensorShape outShape = armnn::TensorShape(outputDims.size(), outputDims.data());

    armnn::TensorInfo outputInfo = inputInfo;
    outputInfo.SetShape(outShape);

    armnn::ReshapeDescriptor reshapeDesc;
    reshapeDesc.m_TargetShape = outputInfo.GetShape();

    const Operand* output = GetOutputOperand(operation, 0, model);
    if (!output)
    {
        return Fail("%s: Could not read output 0", __func__);
    }

    if (!IsLayerSupported(__func__,
                          armnn::IsReshapeSupported,
                          data.m_Compute,
                          inputInfo))
    {
        return false;
    }

    armnn::IConnectableLayer* const layer = data.m_Network->AddReshapeLayer(reshapeDesc);
    assert(layer != nullptr);
    input.Connect(layer->GetInputSlot(0));

    return SetupAndTrackLayerOutputSlot(operation, 0, *layer, model, data);
}

bool HalPolicy::ConvertStridedSlice(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input = ConvertToLayerInputHandle(operation, 0, model, data);
    if (!input.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }
    const armnn::TensorInfo& inputInfo  = input.GetTensorInfo();

    unsigned int rank = inputInfo.GetNumDimensions();
    if (rank > 4)
    {
        Fail("%s: Inputs with rank greater than 4 are not supported", __func__);
    }

    const Operand* beginOperand = GetInputOperand(operation, 1, model);
    const Operand* endOperand = GetInputOperand(operation, 2, model);
    const Operand* stridesOperand = GetInputOperand(operation, 3, model);

    std::vector<int32_t> beginValues;
    std::vector<int32_t> endValues;
    std::vector<int32_t> stridesValues;

    // The length of the beginOperand, endOperand and stridesOperand must be of a rank(input)
    auto ValidateInputOperands = [&] (const Operand& operand, std::vector<int32_t>& operandValues)
    {
        if (!GetTensorInt32Values(operand, operandValues, model, data))
        {
            return false;
        }

        if (operandValues.size() != rank)
        {
            return false;
        }

        return true;
    };

    if (!ValidateInputOperands(*beginOperand, beginValues)
        || !ValidateInputOperands(*endOperand, endValues)
        || !ValidateInputOperands(*stridesOperand, stridesValues))
    {
        return Fail("%s: Operation has invalid input operand", __func__);
    }

    // Stride cannot have value '0'
    if (std::any_of(stridesValues.cbegin(), stridesValues.cend(), [](int32_t i){ return i == 0; }))
    {
        return Fail("%s: Stride must be non-zero value.", __func__);
    }

    armnn::StridedSliceDescriptor descriptor;
    descriptor.m_Begin.assign(beginValues.cbegin(), beginValues.cend());
    descriptor.m_End.assign(endValues.cbegin(), endValues.cend());
    descriptor.m_Stride.assign(stridesValues.cbegin(), stridesValues.cend());
    descriptor.m_DataLayout = armnn::DataLayout::NHWC;

    // Get the "begin_mask", "end_mask", and "shrink_axis_mask" flags
    if (!GetInputInt32(operation, 4, descriptor.m_BeginMask, model, data)
        || !GetInputInt32(operation, 5, descriptor.m_EndMask, model, data)
        || !GetInputInt32(operation, 6, descriptor.m_ShrinkAxisMask, model, data))
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const Operand* output = GetOutputOperand(operation, 0, model);
    if (!output)
    {
        return Fail("%s: Could not read output 0", __func__);
    }
    const armnn::TensorInfo& outputInfo = GetTensorInfoForOperand(*output);

    if (!IsLayerSupported(__func__,
                          armnn::IsStridedSliceSupported,
                          data.m_Compute,
                          inputInfo,
                          outputInfo,
                          descriptor))
    {
        return false;
    }

    armnn::IConnectableLayer* const layer = data.m_Network->AddStridedSliceLayer(descriptor);
    assert(layer != nullptr);
    input.Connect(layer->GetInputSlot(0));

    return SetupAndTrackLayerOutputSlot(operation, 0, *layer, model, data);
}

bool HalPolicy::ConvertTranspose(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input = ConvertToLayerInputHandle(operation, 0, model, data);

    if (!input.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const armnn::TensorInfo& inputInfo  = input.GetTensorInfo();

    unsigned int rank = inputInfo.GetNumDimensions();
    if (rank > 4)
    {
        Fail("%s: Inputs with rank greater than 4 are not supported", __func__);
    }

    // NOTE: Axis is an optional parameter to TRANSPOSE, therefore we do not want to generate a failure
    // if the operand index is out of bounds.
    const Operand* permOperand = GetInputOperand(operation, 1, model, false);

    std::vector<int32_t> perm(rank);
    if (!permOperand)
    {
        // NOTE: If perm is not given, it is set to (n-1...0), where n is the rank of the tensor
        for (unsigned int i = rank; i > 0; i--)
        {
            perm[rank - i] = boost::numeric_cast<int> (i - 1);
        }
    }
    else
    {
        GetTensorInt32Values(*permOperand, perm, model, data);
    }

    std::vector<uint32_t> outputDims(perm.begin(), perm.begin() + rank);

    auto permutationVector = armnn::PermutationVector(outputDims.data(), outputDims.size());
    if (!permutationVector.IsEqual(NHWCToArmNN)
        && !permutationVector.IsEqual(ArmNNToNHWC)
        && !permutationVector.IsEqual({ 3, 2, 0, 1 }))
    {
       return Fail("%s: Only [0, 3, 1, 2], [0, 2, 3, 1] and [3, 2, 0, 1] permutations are supported.", __func__);
    }

    armnn::PermuteDescriptor permuteDesc;
    permuteDesc.m_DimMappings = permutationVector;

    const Operand* output = GetOutputOperand(operation, 0, model);
    if (!output)
    {
        return Fail("%s: Could not read output 0", __func__);
    }

    const armnn::TensorInfo& outputInfo = GetTensorInfoForOperand(*output);

    if (!IsLayerSupported(__func__,
                          armnn::IsPermuteSupported,
                          data.m_Compute,
                          inputInfo,
                          outputInfo,
                          permuteDesc))
    {
        return false;
    }

    armnn::IConnectableLayer* const layer = data.m_Network->AddPermuteLayer(permuteDesc);
    assert(layer != nullptr);
    input.Connect(layer->GetInputSlot(0));

    return SetupAndTrackLayerOutputSlot(operation, 0, *layer, model, data);
}

bool HalPolicy::ConvertBatchToSpaceNd(const Operation& operation, const Model& model, ConversionData& data)
{
    LayerInputHandle input = ConvertToLayerInputHandle(operation, 0, model, data);
    if (!input.IsValid())
    {
        return Fail("%s: Operation has invalid inputs", __func__);
    }

    const Operand* blockOperand = GetInputOperand(operation, 1, model);
    if (!blockOperand)
    {
        return Fail("%s: Could not read input 1", __func__);
    }

    // Convert the block operand to int32
    std::vector<int32_t> block;
    if (!GetTensorInt32Values(*blockOperand, block, model, data))
    {
        return Fail("%s: Input 1 has invalid values", __func__);
    }

    const armnn::TensorInfo& inputInfo = input.GetTensorInfo();

    unsigned int rank = inputInfo.GetNumDimensions();
    if (rank != 4)
    {
        Fail("%s: Only inputs with rank equal to 4 are supported", __func__);
    }

    if (std::any_of(block.cbegin(), block.cend(), [](int32_t i){ return i < 1; }))
    {
        return Fail("%s: Block sizes for each spatial dimension of the input tensor must be"
                    " greater than or equal to 1", __func__);
    }

    armnn::BatchToSpaceNdDescriptor batchToSpaceNdDesc;
    batchToSpaceNdDesc.m_BlockShape.assign(block.cbegin(), block.cend());
    batchToSpaceNdDesc.m_DataLayout = armnn::DataLayout::NHWC;

    // Setting crops to 0,0 0,0 as it is not supported in Android NN API
    batchToSpaceNdDesc.m_Crops = {{0, 0}, {0, 0}};

    const Operand* output = GetOutputOperand(operation, 0, model);
    if (!output)
    {
        return Fail("%s: Could not read output 0", __func__);
    }

    const armnn::TensorInfo& outputInfo = GetTensorInfoForOperand(*output);

    if (!IsLayerSupported(__func__,
                          armnn::IsBatchToSpaceNdSupported,
                          data.m_Compute,
                          inputInfo,
                          outputInfo,
                          batchToSpaceNdDesc))
    {
        return false;
    }

    armnn::IConnectableLayer* const layer = data.m_Network->AddBatchToSpaceNdLayer(batchToSpaceNdDesc);
    assert(layer != nullptr);
    input.Connect(layer->GetInputSlot(0));

    return SetupAndTrackLayerOutputSlot(operation, 0, *layer, model, data);
}


} // namespace hal_1_1
} // namespace armnn_driver