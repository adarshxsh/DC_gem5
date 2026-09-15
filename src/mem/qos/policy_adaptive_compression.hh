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

#ifndef __MEM_QOS_POLICY_ADAPTIVE_COMPRESSION_HH__
#define __MEM_QOS_POLICY_ADAPTIVE_COMPRESSION_HH__

#include <cstdint>

#include "base/compiler.hh"
#include "mem/qos/policy.hh"
#include "params/QoSAdaptiveCompressionQueuePressurePolicy.hh"

namespace gem5
{

namespace memory
{

namespace qos
{

/**
 * Adaptive Compression & Queue Pressure QoS Policy
 *
 * Dynamically adjusts request priorities based on packet payload
 * compression ratio and memory controller queue pressure gradients.
 */
class AdaptiveCompressionQueuePressurePolicy : public Policy
{
    using Params = QoSAdaptiveCompressionQueuePressurePolicyParams;

  public:
    AdaptiveCompressionQueuePressurePolicy(const Params &p);
    virtual ~AdaptiveCompressionQueuePressurePolicy();

    uint8_t schedule(const RequestorID id, const uint64_t data) override;
    uint8_t schedule(const PacketPtr pkt) override;

  protected:
    /** Write queue pressure threshold ratio (0.0 to 1.0) */
    const double writePressureThreshold;

    /** Read queue pressure threshold ratio (0.0 to 1.0) */
    const double readPressureThreshold;

    /** Minimum payload compression ratio to trigger writeback priority boost
     */
    const double compressionRatioThreshold;

    /** Maximum priority boost for compressed writebacks */
    const uint8_t writeboostMax;

    /** Maximum priority boost for latency-critical reads */
    const uint8_t readboostMax;

    /** Default priority for non-boosted requests */
    const uint8_t defaultPriority;
};

} // namespace qos
} // namespace memory
} // namespace gem5

#endif // __MEM_QOS_POLICY_ADAPTIVE_COMPRESSION_HH__
