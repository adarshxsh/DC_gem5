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

#include <gtest/gtest.h>

#include "mem/packet.hh"
#include "mem/qos/mem_ctrl.hh"
#include "mem/qos/policy_adaptive_compression.hh"
#include "mem/qos/q_policy.hh"
#include "params/QoSAdaptiveCompressionQueuePressurePolicy.hh"
#include "params/QoSMemCtrl.hh"

namespace gem5
{

namespace sim_clock
{
namespace as_float
{
double s = 1.0e-12;
}
} // namespace sim_clock

namespace memory
{
namespace qos
{

TEST(PacketCompressionTest, CompressionMetadata)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WriteReq);
    pkt.setCompressedSize(16); // 4:1 compression

    EXPECT_TRUE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSize(), 16);
    EXPECT_EQ(pkt.getCompressedSizeBits(), 128);
    EXPECT_DOUBLE_EQ(pkt.getCompressionRatio(), 4.0);
}

TEST(PacketCompressionTest, UncompressedPacket)
{
    RequestPtr req = std::make_shared<Request>(0x2000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSize(), 64);
    EXPECT_DOUBLE_EQ(pkt.getCompressionRatio(), 1.0);
}

TEST(AdaptiveQoSPolicyTest, WritebackPriorityBoost)
{
    QoSAdaptiveCompressionQueuePressurePolicyParams p;
    p.write_pressure_threshold = 0.5;
    p.read_pressure_threshold = 0.5;
    p.compression_ratio_threshold = 1.2;
    p.writeboost_max = 3;
    p.readboost_max = 2;
    p.default_prio = 1;
    p.name = "test_policy";

    AdaptiveCompressionQueuePressurePolicy policy(p);

    class MockMemCtrl : public MemCtrl
    {
      public:
        MockMemCtrl(const QoSMemCtrlParams &p) : MemCtrl(p) {}
        void
        setReadFill(double f)
        {
            custom_rd_fill = f;
        }
        void
        setWriteFill(double f)
        {
            custom_wr_fill = f;
        }
        void
        setReadGrad(double g)
        {
            custom_rd_grad = g;
        }
        void
        setWriteGrad(double g)
        {
            custom_wr_grad = g;
        }

        double
        getReadQueueFillRatio() const override
        {
            return custom_rd_fill;
        }
        double
        getWriteQueueFillRatio() const override
        {
            return custom_wr_fill;
        }
        double
        getReadQueuePressureGradient() const override
        {
            return custom_rd_grad;
        }
        double
        getWriteQueuePressureGradient() const override
        {
            return custom_wr_grad;
        }

      private:
        double custom_rd_fill = 0.0;
        double custom_wr_fill = 0.0;
        double custom_rd_grad = 0.0;
        double custom_wr_grad = 0.0;
    };

    QoSMemCtrlParams mc_params;
    mc_params.qos_priorities = 8;
    mc_params.qos_priority_escalation = false;
    mc_params.qos_syncro_scheduler = false;
    mc_params.qos_q_policy = enums::QoSQPolicy::fifo;
    mc_params.qos_policy = nullptr;
    mc_params.qos_turnaround_policy = nullptr;
    mc_params.system = nullptr;
    mc_params.name = "test_ctrl";

    MockMemCtrl mock_ctrl(mc_params);
    policy.setMemCtrl(&mock_ctrl);

    RequestPtr req_wr = std::make_shared<Request>(0x3000, 64, 0, 0);
    Packet pkt_wr(req_wr, MemCmd::WriteReq);
    pkt_wr.setCompressedSize(16); // 4:1 compression ratio

    // Low write queue pressure -> no boost
    mock_ctrl.setWriteFill(0.2);
    mock_ctrl.setWriteGrad(0.0);
    EXPECT_EQ(policy.schedule(&pkt_wr), 1);

    // High write queue pressure -> priority boosted
    mock_ctrl.setWriteFill(0.8);
    mock_ctrl.setWriteGrad(0.1);
    EXPECT_GT(policy.schedule(&pkt_wr), 1);
}

TEST(AdaptiveQoSQueuePolicyTest, SelectCompressedPacketOnSpike)
{
    QoSMemCtrlParams mc_params;
    mc_params.qos_priorities = 8;
    mc_params.qos_priority_escalation = false;
    mc_params.qos_syncro_scheduler = false;
    mc_params.qos_q_policy = enums::QoSQPolicy::compression_aware;
    mc_params.qos_policy = nullptr;
    mc_params.qos_turnaround_policy = nullptr;
    mc_params.system = nullptr;
    mc_params.name = "test_ctrl_q";

    class MockMemCtrl : public MemCtrl
    {
      public:
        MockMemCtrl(const QoSMemCtrlParams &p) : MemCtrl(p) {}
        void
        setWriteFill(double f)
        {
            custom_wr_fill = f;
        }
        double
        getWriteQueueFillRatio() const override
        {
            return custom_wr_fill;
        }
        double
        getWriteQueuePressureGradient() const override
        {
            return 0.2;
        }

      private:
        double custom_wr_fill = 0.8;
    };

    MockMemCtrl mock_ctrl(mc_params);
    std::unique_ptr<QueuePolicy> q_pol(QueuePolicy::create(mc_params));
    q_pol->setMemCtrl(&mock_ctrl);

    RequestPtr req1 = std::make_shared<Request>(0x100, 64, 0, 0);
    Packet pkt1(req1, MemCmd::WriteReq); // Uncompressed

    RequestPtr req2 = std::make_shared<Request>(0x200, 64, 0, 0);
    Packet pkt2(req2, MemCmd::WriteReq);
    pkt2.setCompressedSize(16); // Compressed

    QueuePolicy::PacketQueue queue;
    queue.push_back(&pkt1);
    queue.push_back(&pkt2);

    auto sel_it = q_pol->selectPacket(&queue);
    EXPECT_EQ(*sel_it,
              &pkt2); // Compressed packet selected first during pressure spike
}

} // namespace qos
} // namespace memory
} // namespace gem5
