/*
 * Copyright (c) 2024 ARM Limited
 * All rights reserved
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

#include "mem/qos/policy_adaptive_compression.hh"

#include <algorithm>
#include <cmath>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/QOS.hh"

namespace gem5
{

namespace memory
{

namespace qos
{

AdaptiveCompressionQueuePressurePolicy::AdaptiveCompressionQueuePressurePolicy(
    const Params &p)
    : Policy(p),
      writePressureThreshold(p.write_pressure_threshold),
      readPressureThreshold(p.read_pressure_threshold),
      compressionRatioThreshold(p.compression_ratio_threshold),
      writeboostMax(p.writeboost_max),
      readboostMax(p.readboost_max),
      defaultPriority(p.default_prio)
{}

AdaptiveCompressionQueuePressurePolicy::
    ~AdaptiveCompressionQueuePressurePolicy()
{}

uint8_t
AdaptiveCompressionQueuePressurePolicy::schedule(const RequestorID id,
                                                 const uint64_t data)
{
    return defaultPriority;
}

uint8_t
AdaptiveCompressionQueuePressurePolicy::schedule(const PacketPtr pkt)
{
    uint8_t base_prio = defaultPriority;
    if (pkt->qosValue() > 0) {
        base_prio = pkt->qosValue();
    }

    if (!memCtrl) {
        return base_prio;
    }

    uint8_t max_prio =
        memCtrl->numPriorities() > 0 ? (memCtrl->numPriorities() - 1) : 0;
    uint8_t calculated_prio = base_prio;

    if (pkt->isWrite() || pkt->isWriteback()) {
        double wr_fill = memCtrl->getWriteQueueFillRatio();
        double wr_grad = memCtrl->getWriteQueuePressureGradient();
        double comp_ratio = pkt->getCompressionRatio();

        if (wr_fill >= writePressureThreshold || wr_grad > 0.0) {
            if (pkt->isCompressed() ||
                comp_ratio >= compressionRatioThreshold) {
                double pressure_factor =
                    std::max(0.0, wr_fill + std::max(0.0, wr_grad));
                uint8_t boost = static_cast<uint8_t>(std::min<double>(
                    writeboostMax,
                    std::round(comp_ratio * pressure_factor * writeboostMax)));
                if (boost == 0 && (wr_fill >= writePressureThreshold ||
                                   comp_ratio >= compressionRatioThreshold)) {
                    boost = 1;
                }
                calculated_prio =
                    std::min<uint8_t>(max_prio, base_prio + boost);
            }
        }
    } else if (pkt->isRead()) {
        double rd_fill = memCtrl->getReadQueueFillRatio();
        double rd_grad = memCtrl->getReadQueuePressureGradient();
        double wr_fill = memCtrl->getWriteQueueFillRatio();

        if (rd_fill >= readPressureThreshold || rd_grad > 0.0 ||
            wr_fill >= writePressureThreshold) {
            double pressure_factor =
                std::max(0.0, rd_fill + std::max(0.0, rd_grad));
            uint8_t boost = static_cast<uint8_t>(std::min<double>(
                readboostMax, std::round(pressure_factor * readboostMax)));
            if (boost == 0 &&
                (rd_fill >= readPressureThreshold || rd_grad > 0.0)) {
                boost = 1;
            }
            calculated_prio = std::min<uint8_t>(max_prio, base_prio + boost);
        }
    }

    DPRINTF(QOS,
            "AdaptiveCompressionQueuePressurePolicy: pkt addr %#x type %s "
            "compRatio %.2f base_prio %d calculated_prio %d\n",
            pkt->getAddr(), pkt->isWrite() ? "WRITE" : "READ",
            pkt->getCompressionRatio(), base_prio, calculated_prio);

    return std::min<uint8_t>(max_prio, calculated_prio);
}

} // namespace qos
} // namespace memory
} // namespace gem5
