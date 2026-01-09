/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kernel_operator.h"
using namespace AscendC;
extern "C" __global__ __aicore__ void DemoTest(GM_ADDR input, GM_ADDR sqei) {
    if ASCEND_IS_AIV {
        if (GetBlockIdx() != 0) {
            return;
        }
        __ubuf__ int8_t* sqeUb = (__ubuf__ int8_t*)(128);
        copy_gm_to_ubuf_align_b8(sqeUb, input, 0, 1, 32, 0, 0, 0, 0);
        AscendC::SetFlag<HardEvent::MTE2_MTE3>(EVENT_ID0);
        AscendC::WaitFlag<HardEvent::MTE2_MTE3>(EVENT_ID0);
        copy_ubuf_to_gm_align_b8(sqei, sqeUb, 0, 1, 32, 0, 0, 0, 0);
    }
}
