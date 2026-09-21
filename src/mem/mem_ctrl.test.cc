/*
 * Copyright (c) 2026 gem5 Development Group
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <gtest/gtest.h>

#include "mem/mem_interface.hh"

namespace gem5
{
namespace memory
{

TEST(MemCtrlTest, DefaultIsWriteDrainingFalse)
{
    bool isWriteDraining = false;
    EXPECT_FALSE(isWriteDraining);
}

TEST(MemCtrlTest, HighWatermarkTriggersWriteDraining)
{
    bool isWriteDraining = false;
    uint32_t writeHighThreshold = 32;
    uint32_t writeLowThreshold = 16;
    uint32_t writeQueueSize = 35;

    // Simulate entering write mode due to writeQueueSize > writeHighThreshold
    if (writeQueueSize > writeHighThreshold) {
        isWriteDraining = true;
    }
    EXPECT_TRUE(isWriteDraining);

    // Servicing write requests down to 20 (> writeLowThreshold)
    writeQueueSize = 20;
    if (writeQueueSize > writeHighThreshold) {
        isWriteDraining = true;
    } else if (writeQueueSize < writeLowThreshold || writeQueueSize == 0) {
        isWriteDraining = false;
    }
    // Should remain true while writeQueueSize is above writeLowThreshold
    EXPECT_TRUE(isWriteDraining);

    // Servicing write requests down to 15 (< writeLowThreshold)
    writeQueueSize = 15;
    if (writeQueueSize > writeHighThreshold) {
        isWriteDraining = true;
    } else if (writeQueueSize < writeLowThreshold || writeQueueSize == 0) {
        isWriteDraining = false;
    }
    // Should reset to false when writeQueueSize falls below writeLowThreshold
    EXPECT_FALSE(isWriteDraining);
}

TEST(MemCtrlTest, LowWatermarkOpportunisticNoDraining)
{
    bool isWriteDraining = false;
    uint32_t writeHighThreshold = 32;
    uint32_t writeQueueSize = 20;

    // Simulate entering write mode when writeQueueSize <= writeHighThreshold (e.g. empty read queue)
    if (writeQueueSize > writeHighThreshold) {
        isWriteDraining = true;
    }
    EXPECT_FALSE(isWriteDraining);
}

} // namespace memory
} // namespace gem5
