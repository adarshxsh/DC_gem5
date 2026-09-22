/*
 * Copyright (c) 2024 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
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

#include "mem/qos/turnaround_policy_pressure.hh"

#include <algorithm>
#include <cstdint>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/QOS.hh"

namespace gem5
{

namespace memory
{

namespace qos
{

TurnaroundPolicyPressure::TurnaroundPolicyPressure(const Params &p)
    : TurnaroundPolicy(p),
      alpha(p.pressure_alpha),
      hysteresis(p.turnaround_hysteresis),
      minBurstLength(p.min_burst_length),
      readPressure(0.0),
      writePressure(0.0),
      readFillRate(0.0),
      writeFillRate(0.0),
      prevReadQueueSize(0),
      prevWriteQueueSize(0),
      burstCount(0),
      initialized(false)
{
    fatal_if(
        alpha <= 0.0 || alpha > 1.0,
        "QoSTurnaroundPolicyPressure: pressure_alpha must be in (0.0, 1.0]");
}

TurnaroundPolicyPressure::~TurnaroundPolicyPressure()
{}

MemCtrl::BusState
TurnaroundPolicyPressure::selectBusState()
{
    auto current_state = memCtrl->getBusState();
    const auto num_priorities = memCtrl->numPriorities();

    const uint64_t total_read = memCtrl->getTotalReadQueueSize();
    const uint64_t total_write = memCtrl->getTotalWriteQueueSize();

    double weighted_read = 0.0;
    double weighted_write = 0.0;

    for (uint8_t i = 0; i < num_priorities; i++) {
        weighted_read += (i + 1) * memCtrl->getReadQueueSize(i);
        weighted_write += (i + 1) * memCtrl->getWriteQueueSize(i);
    }

    if (!initialized) {
        readPressure = weighted_read;
        writePressure = weighted_write;
        readFillRate = 0.0;
        writeFillRate = 0.0;
        prevReadQueueSize = total_read;
        prevWriteQueueSize = total_write;
        burstCount = 0;
        initialized = true;
    } else {
        double delta_read = static_cast<double>(total_read) -
                            static_cast<double>(prevReadQueueSize);
        double delta_write = static_cast<double>(total_write) -
                             static_cast<double>(prevWriteQueueSize);

        prevReadQueueSize = total_read;
        prevWriteQueueSize = total_write;

        readFillRate = alpha * delta_read + (1.0 - alpha) * readFillRate;
        writeFillRate = alpha * delta_write + (1.0 - alpha) * writeFillRate;

        readPressure = alpha * (weighted_read + readFillRate) +
                       (1.0 - alpha) * readPressure;
        writePressure = alpha * (weighted_write + writeFillRate) +
                        (1.0 - alpha) * writePressure;
    }

    double delta_p = writePressure - readPressure;
    MemCtrl::BusState bus_state = current_state;

    if (total_read == 0 && total_write == 0) {
        bus_state = current_state;
    } else if (total_read == 0 && total_write > 0) {
        bus_state = MemCtrl::WRITE;
    } else if (total_write == 0 && total_read > 0) {
        bus_state = MemCtrl::READ;
    } else {
        uint32_t threshold = std::min(hysteresis, minBurstLength);
        if (burstCount < threshold) {
            bus_state = current_state;
        } else {
            if (current_state == MemCtrl::READ) {
                if (delta_p > 0.0) {
                    bus_state = MemCtrl::WRITE;
                } else {
                    bus_state = MemCtrl::READ;
                }
            } else {
                if (delta_p < 0.0) {
                    bus_state = MemCtrl::READ;
                } else {
                    bus_state = MemCtrl::WRITE;
                }
            }
        }
    }

    if (bus_state != current_state) {
        burstCount = 0;
    } else {
        burstCount++;
    }

    DPRINTF(QOS,
            "QoSMemoryTurnaround::QoSTurnaroundPolicyPressure - "
            "read Pressure %.2f, write Pressure %.2f, deltaP %.2f, "
            "burstCount %d triggering bus %s in state %s\n",
            readPressure, writePressure, delta_p, burstCount,
            (bus_state != current_state) ? "turnaround" : "staying",
            (bus_state == MemCtrl::READ) ? "READ" : "WRITE");

    return bus_state;
}

} // namespace qos
} // namespace memory
} // namespace gem5
