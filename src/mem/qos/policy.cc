/*
 * Copyright (c) 2018 ARM Limited
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

#include "mem/qos/policy.hh"

#include "params/QoSPolicy.hh"

namespace gem5
{

namespace memory
{

namespace qos
{

Policy::Policy(const Params &p)
  : SimObject(p),
    enableCompressionAwareness(p.enable_compression_awareness)
{}

Policy::~Policy() {}

uint8_t
Policy::schedule(const PacketPtr pkt)
{
    assert(pkt->req);

    if (enableCompressionAwareness && pkt->isCompressed()) {
        double cr = pkt->getCompressionRatio();
        uint64_t eff_len = pkt->getCompressedSize();
        uint8_t base_prio = schedule(pkt->req->requestorId(), eff_len);
        uint8_t num_prios = memCtrl ? memCtrl->numPriorities() : 16;

        uint8_t bonus = static_cast<uint8_t>(
            std::min<double>(num_prios - 1, cr - 1.0));
        uint8_t final_prio = base_prio + bonus;
        if (final_prio >= num_prios) {
            final_prio = num_prios - 1;
        }
        DPRINTF(QOS, "QoSPolicy::schedule compression-aware: req %d, "
                     "comp_size %d, orig_size %d, ratio %.2f, "
                     "base_prio %d -> final_prio %d\n",
                     pkt->req->requestorId(), eff_len, pkt->getSize(),
                     cr, base_prio, final_prio);
        return final_prio;
    }

    return schedule(pkt->req->requestorId(), pkt->getSize());
}

} // namespace qos
} // namespace memory
} // namespace gem5
