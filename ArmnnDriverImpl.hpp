//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#pragma once

#include "DriverOptions.hpp"

#include <HalInterfaces.h>

namespace armnn_driver
{

template<typename HalPolicy>
class ArmnnDriverImpl
{
public:
    using HalModel                     = typename HalPolicy::Model;
    using HalGetSupportedOperations_cb = typename HalPolicy::getSupportedOperations_cb;

    static Return<void> getSupportedOperations(
            const armnn::IRuntimePtr& runtime,
            const DriverOptions& options,
            const HalModel& model,
            HalGetSupportedOperations_cb);

    static Return<ErrorStatus> prepareModel(
            const armnn::IRuntimePtr& runtime,
            const armnn::IGpuAccTunedParametersPtr& clTunedParameters,
            const DriverOptions& options,
            const HalModel& model,
            const android::sp<IPreparedModelCallback>& cb,
            bool float32ToFloat16 = false);

    static Return<DeviceStatus> getStatus();
};

} // namespace armnn_driver
