/*
 * Copyright (c) 2026
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

#include "mem/filtered_feedback_controller.hh"

using namespace gem5;

TEST(FilteredFeedbackControllerTest, EMASmoothingAndSeeding)
{
    // alpha = 0.5, high_wm = 10.0, low_wm = 5.0, min_residency = 0
    FilteredFeedbackController ctrl(0.5, 10.0, 5.0, 0, true, true, false);

    EXPECT_FALSE(ctrl.isInitialized());

    // First sample seeds the EMA smoothed value directly
    ctrl.update(10.0, 100);
    EXPECT_TRUE(ctrl.isInitialized());
    EXPECT_DOUBLE_EQ(ctrl.getSmoothedValue(), 10.0);

    // Second sample: smoothed = 0.5 * 20.0 + 0.5 * 10.0 = 15.0
    ctrl.update(20.0, 200);
    EXPECT_DOUBLE_EQ(ctrl.getSmoothedValue(), 15.0);

    // Third sample: smoothed = 0.5 * 0.0 + 0.5 * 15.0 = 7.5
    ctrl.update(0.0, 300);
    EXPECT_DOUBLE_EQ(ctrl.getSmoothedValue(), 7.5);
}

TEST(FilteredFeedbackControllerTest, DualThresholdHysteresis)
{
    // alpha = 0.5, high_wm = 1.2, low_wm = 1.0, min_residency = 0
    FilteredFeedbackController ctrl(0.5, 1.2, 1.0, 0, true, true, false);

    // Initial state: false (LOW)
    EXPECT_FALSE(ctrl.getState());

    // Input ratio 1.1 -> seeded 1.1 <= high_wm (1.2), remains false
    ctrl.update(1.1, 100);
    EXPECT_FALSE(ctrl.getState());

    // Input ratio 1.5 -> smoothed = 0.5 * 1.5 + 0.5 * 1.1 = 1.3 > 1.2 ->
    // transitions to true (HIGH)
    ctrl.update(1.5, 200);
    EXPECT_TRUE(ctrl.getState());

    // Input ratio 1.05 -> smoothed = 0.5 * 1.05 + 0.5 * 1.3 = 1.175 (in
    // deadband between 1.0 and 1.2) -> stays true
    ctrl.update(1.05, 300);
    EXPECT_TRUE(ctrl.getState());

    // Input ratio 0.5 -> smoothed = 0.5 * 0.5 + 0.5 * 1.175 = 0.8375 < 1.0 ->
    // transitions to false (LOW)
    ctrl.update(0.5, 400);
    EXPECT_FALSE(ctrl.getState());
}

TEST(FilteredFeedbackControllerTest, MinimumResidencyTicks)
{
    // min_residency_ticks = 1000
    FilteredFeedbackController ctrl(1.0, 10.0, 5.0, 1000, false, true, false);

    // Initial state false at tick 0
    ctrl.update(20.0, 100); // Exceeds high_wm (10.0), but tick 100 < last(0) +
                            // 1000 -> remains false
    EXPECT_FALSE(ctrl.getState());

    // At tick 1000: residency met -> transitions to true
    ctrl.update(20.0, 1000);
    EXPECT_TRUE(ctrl.getState());
    EXPECT_EQ(ctrl.getLastStateChangeTick(), 1000ULL);

    // Signal drops below low_wm (5.0) at tick 1500, but tick 1500 < 1000 +
    // 1000 -> remains true
    ctrl.update(1.0, 1500);
    EXPECT_TRUE(ctrl.getState());

    // At tick 2000: residency met -> transitions to false
    ctrl.update(1.0, 2000);
    EXPECT_FALSE(ctrl.getState());
    EXPECT_EQ(ctrl.getLastStateChangeTick(), 2000ULL);
}

TEST(FilteredFeedbackControllerTest, FallbackModes)
{
    // Disabling EMA: raw value passed directly
    FilteredFeedbackController ctrlNoEMA(0.1, 10.0, 5.0, 0, false, true,
                                         false);
    ctrlNoEMA.update(15.0, 100);
    EXPECT_TRUE(ctrlNoEMA.getState());
    EXPECT_DOUBLE_EQ(ctrlNoEMA.getSmoothedValue(), 15.0);

    // Disabling Hysteresis: single threshold comparison (>= high_wm)
    FilteredFeedbackController ctrlNoHyst(0.5, 10.0, 5.0, 0, true, false,
                                          false);
    ctrlNoHyst.update(8.0, 100); // 8.0 < 10.0 -> false
    EXPECT_FALSE(ctrlNoHyst.getState());

    ctrlNoHyst.update(12.0,
                      200); // smoothed = 0.5*12 + 0.5*8 = 10.0 >= 10.0 -> true
    EXPECT_TRUE(ctrlNoHyst.getState());

    ctrlNoHyst.update(8.0,
                      300); // smoothed = 0.5*8 + 0.5*10 = 9.0 < 10.0 -> false
    EXPECT_FALSE(ctrlNoHyst.getState());
}
