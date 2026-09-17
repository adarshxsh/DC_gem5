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
 * Declaration of FilteredFeedbackController combining EMA signal smoothing
 * with dual-threshold hysteresis windowing and minimum residency dwell times.
 */

#ifndef __MEM_FILTERED_FEEDBACK_CONTROLLER_HH__
#define __MEM_FILTERED_FEEDBACK_CONTROLLER_HH__

#include <cstdint>
#include <string>

#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5
{

/**
 * FilteredFeedbackController combines Exponential Moving Average (EMA)
 * signal smoothing with dual-threshold hysteresis windowing and
 * minimum residency dwell time constraints.
 */
class FilteredFeedbackController
{
  private:
    /** EMA smoothing factor alpha (0.0 <= alpha <= 1.0). */
    double alpha;

    /** High watermark threshold. */
    double highWatermark;

    /** Low watermark threshold. */
    double lowWatermark;

    /** Minimum residency dwell time in simulation ticks before state transition. */
    Tick minResidencyTicks;

    /** Controls whether EMA smoothing is enabled. */
    bool enableEMA;

    /** Controls whether hysteresis windowing is enabled. */
    bool enableHysteresis;

    /** Current EMA smoothed value. */
    double smoothedValue;

    /** Current state (true = HIGH / ACTIVE, false = LOW / BYPASSED). */
    bool currentState;

    /** Tick timestamp of the last state transition. */
    Tick lastStateChangeTick;

    /** Tracks whether smoothedValue has been initialized with the first sample. */
    bool initialized;

  public:
    FilteredFeedbackController(double _alpha = 0.125,
                               double _high_wm = 1.0,
                               double _low_wm = 0.0,
                               Tick _min_residency_ticks = 0,
                               bool _enable_ema = true,
                               bool _enable_hysteresis = true,
                               bool _initial_state = false);

    ~FilteredFeedbackController() = default;

    /**
     * Configure or re-configure controller parameters.
     */
    void setParams(double _alpha, double _high_wm, double _low_wm,
                   Tick _min_residency_ticks,
                   bool _enable_ema = true, bool _enable_hysteresis = true);

    /**
     * Process a new raw signal sample at cur_tick, update EMA, and evaluate state transitions.
     *
     * @param raw_val Raw signal sample.
     * @param cur_tick Current simulation tick.
     * @return Current state of the controller after evaluation.
     */
    bool update(double raw_val, Tick cur_tick);

    /** Getters */
    double getSmoothedValue() const { return smoothedValue; }
    bool getState() const { return currentState; }
    Tick getLastStateChangeTick() const { return lastStateChangeTick; }
    bool isInitialized() const { return initialized; }
    double getAlpha() const { return alpha; }
    double getHighWatermark() const { return highWatermark; }
    double getLowWatermark() const { return lowWatermark; }
    Tick getMinResidencyTicks() const { return minResidencyTicks; }
    bool isEMAEnabled() const { return enableEMA; }
    bool isHysteresisEnabled() const { return enableHysteresis; }

    /** Setters */
    void setState(bool s, Tick cur_tick = 0);
    void reset();

    /** Checkpoint serialization and restoration */
    void serialize(CheckpointOut &cp, const std::string &section) const;
    void unserialize(CheckpointIn &cp, const std::string &section);
};

} // namespace gem5

#endif // __MEM_FILTERED_FEEDBACK_CONTROLLER_HH__
