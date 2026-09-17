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

/**
 * @file
 * Implementation of FilteredFeedbackController combining EMA signal smoothing
 * with dual-threshold hysteresis windowing and minimum residency dwell times.
 */

#include "mem/filtered_feedback_controller.hh"

namespace gem5
{

FilteredFeedbackController::FilteredFeedbackController(
    double _alpha, double _high_wm, double _low_wm, Tick _min_residency_ticks,
    bool _enable_ema, bool _enable_hysteresis, bool _initial_state)
    : alpha(_alpha),
      highWatermark(_high_wm),
      lowWatermark(_low_wm),
      minResidencyTicks(_min_residency_ticks),
      enableEMA(_enable_ema),
      enableHysteresis(_enable_hysteresis),
      smoothedValue(0.0),
      currentState(_initial_state),
      lastStateChangeTick(0),
      initialized(false)
{}

void
FilteredFeedbackController::setParams(double _alpha, double _high_wm,
                                      double _low_wm,
                                      Tick _min_residency_ticks,
                                      bool _enable_ema,
                                      bool _enable_hysteresis)
{
    alpha = _alpha;
    highWatermark = _high_wm;
    lowWatermark = _low_wm;
    minResidencyTicks = _min_residency_ticks;
    enableEMA = _enable_ema;
    enableHysteresis = _enable_hysteresis;
}

bool
FilteredFeedbackController::update(double raw_val, Tick cur_tick)
{
    // 1. Apply EMA smoothing if enabled, otherwise raw signal
    if (enableEMA) {
        if (!initialized) {
            smoothedValue = raw_val;
            initialized = true;
        } else {
            smoothedValue = alpha * raw_val + (1.0 - alpha) * smoothedValue;
        }
    } else {
        smoothedValue = raw_val;
        initialized = true;
    }

    // Signal used for evaluating thresholds
    double eval_signal = enableEMA ? smoothedValue : raw_val;

    // 2. Evaluate state transitions using dual-threshold hysteresis and
    // residency dwell time
    if (enableHysteresis) {
        bool residency_met =
            (cur_tick >= lastStateChangeTick + minResidencyTicks);
        if (!currentState) {
            // Currently in LOW state: transition to HIGH if signal exceeds
            // high watermark and dwell time met
            if ((eval_signal > highWatermark) && residency_met) {
                currentState = true;
                lastStateChangeTick = cur_tick;
            }
        } else {
            // Currently in HIGH state: transition to LOW if signal drops below
            // low watermark and dwell time met
            if ((eval_signal < lowWatermark) && residency_met) {
                currentState = false;
                lastStateChangeTick = cur_tick;
            }
        }
    } else {
        // Fallback without hysteresis deadband: direct threshold comparison
        currentState = (eval_signal >= highWatermark);
    }

    return currentState;
}

void
FilteredFeedbackController::setState(bool s, Tick cur_tick)
{
    currentState = s;
    lastStateChangeTick = cur_tick;
}

void
FilteredFeedbackController::reset()
{
    smoothedValue = 0.0;
    currentState = false;
    lastStateChangeTick = 0;
    initialized = false;
}

void
FilteredFeedbackController::serialize(CheckpointOut &cp,
                                      const std::string &section) const
{
    paramOut(cp, section + ".alpha", alpha);
    paramOut(cp, section + ".highWatermark", highWatermark);
    paramOut(cp, section + ".lowWatermark", lowWatermark);
    paramOut(cp, section + ".minResidencyTicks", minResidencyTicks);
    paramOut(cp, section + ".enableEMA", enableEMA);
    paramOut(cp, section + ".enableHysteresis", enableHysteresis);
    paramOut(cp, section + ".smoothedValue", smoothedValue);
    paramOut(cp, section + ".currentState", currentState);
    paramOut(cp, section + ".lastStateChangeTick", lastStateChangeTick);
    paramOut(cp, section + ".initialized", initialized);
}

void
FilteredFeedbackController::unserialize(CheckpointIn &cp,
                                        const std::string &section)
{
    paramIn(cp, section + ".alpha", alpha);
    paramIn(cp, section + ".highWatermark", highWatermark);
    paramIn(cp, section + ".lowWatermark", lowWatermark);
    paramIn(cp, section + ".minResidencyTicks", minResidencyTicks);
    paramIn(cp, section + ".enableEMA", enableEMA);
    paramIn(cp, section + ".enableHysteresis", enableHysteresis);
    paramIn(cp, section + ".smoothedValue", smoothedValue);
    paramIn(cp, section + ".currentState", currentState);
    paramIn(cp, section + ".lastStateChangeTick", lastStateChangeTick);
    paramIn(cp, section + ".initialized", initialized);
}

} // namespace gem5
