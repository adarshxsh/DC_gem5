/*
 * Copyright (c) 2026 gem5
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

#include "mem/ruby/network/Network.hh"
#include "mem/ruby/network/garnet/flit.hh"
#include "mem/ruby/slicc_interface/Message.hh"
#include "mem/ruby/system/RubySystem.hh"

namespace gem5
{
namespace ruby
{

// Initialize static network size variables for testing
uint32_t Network::m_control_msg_size = 8;
uint32_t Network::m_data_msg_size = 72;

int
MachineType_base_level(const MachineType &)
{
    return 1;
}
MachineType
MachineType_from_base_level(int)
{
    return static_cast<MachineType>(0);
}
MachineType &
operator++(MachineType &m)
{
    return m;
}

int
RubySystem::MachineType_base_count(const MachineType &)
{
    return 0;
}
int
RubySystem::MachineType_base_number(const MachineType &)
{
    return 0;
}

uint32_t
Network::MessageSizeType_to_int(MessageSizeType size_type, const Message *msg)
{
    if (msg && msg->getPayloadSize() > 0) {
        return msg->getPayloadSize() + m_control_msg_size;
    }

    switch (size_type) {
        case MessageSizeType_Control:
        case MessageSizeType_Request_Control:
        case MessageSizeType_Reissue_Control:
        case MessageSizeType_Response_Control:
        case MessageSizeType_Writeback_Control:
        case MessageSizeType_Broadcast_Control:
        case MessageSizeType_Multicast_Control:
        case MessageSizeType_Forwarded_Control:
        case MessageSizeType_Invalidate_Control:
        case MessageSizeType_Unblock_Control:
        case MessageSizeType_Persistent_Control:
        case MessageSizeType_Completion_Control:
            return m_control_msg_size;
        case MessageSizeType_Data:
        case MessageSizeType_Response_Data:
        case MessageSizeType_ResponseLocal_Data:
        case MessageSizeType_ResponseL2hit_Data:
        case MessageSizeType_Writeback_Data:
            return m_data_msg_size;
        default:
            panic("Invalid range for type MessageSizeType");
            break;
    }
}

class TestMessage : public Message
{
  public:
    TestMessage(MessageSizeType size_type, Tick curTime = 0,
                int block_size = 64)
        : Message(curTime, block_size, nullptr), m_size_type(size_type)
    {}

    MsgPtr
    clone() const override
    {
        return std::shared_ptr<Message>(new TestMessage(*this));
    }

    void
    print(std::ostream &out) const override
    {
        out << "[TestMessage]";
    }

    const MessageSizeType &
    getMessageSize() const override
    {
        return m_size_type;
    }

    MessageSizeType &
    getMessageSize() override
    {
        return m_size_type;
    }

  private:
    MessageSizeType m_size_type;
};

TEST(MessageTest, DefaultPayloadSizeIsZero)
{
    TestMessage msg(MessageSizeType_Data);
    EXPECT_EQ(msg.getPayloadSize(), 0);
}

TEST(MessageTest, SetAndGetPayloadSize)
{
    TestMessage msg(MessageSizeType_Data);
    msg.setPayloadSize(16);
    EXPECT_EQ(msg.getPayloadSize(), 16);
}

TEST(MessageTest, ClonedMessagePreservesPayloadSize)
{
    TestMessage msg(MessageSizeType_Response_Data);
    msg.setPayloadSize(24);
    MsgPtr cloned = msg.clone();
    EXPECT_EQ(cloned->getPayloadSize(), 24);
}

TEST(NetworkTest, DynamicPayloadSizeResolution)
{
    TestMessage msg(MessageSizeType_Response_Data);

    // Fallback to static data message size when payload size is 0
    uint32_t static_size =
        Network::MessageSizeType_to_int(msg.getMessageSize(), &msg);
    EXPECT_GT(static_size, 0);

    // Dynamic payload size resolution when payload size > 0
    msg.setPayloadSize(16);
    uint32_t dynamic_size =
        Network::MessageSizeType_to_int(msg.getMessageSize(), &msg);
    uint32_t ctrl_size =
        Network::MessageSizeType_to_int(MessageSizeType_Control);
    EXPECT_EQ(dynamic_size, 16 + ctrl_size);
}

TEST(SimpleNetworkThrottleTest, DynamicMessageToSizeCalculation)
{
    TestMessage uncompressed_msg(MessageSizeType_Response_Data);
    EXPECT_EQ(uncompressed_msg.getPayloadSize(), 0);

    uint32_t uncompressed_size = Network::MessageSizeType_to_int(
        uncompressed_msg.getMessageSize(), &uncompressed_msg);

    TestMessage compressed_msg(MessageSizeType_Response_Data);
    compressed_msg.setPayloadSize(16);

    uint32_t compressed_size = Network::MessageSizeType_to_int(
        compressed_msg.getMessageSize(), &compressed_msg);
    uint32_t ctrl_size =
        Network::MessageSizeType_to_int(MessageSizeType_Control);
    EXPECT_EQ(compressed_size, 16 + ctrl_size);
    EXPECT_LT(compressed_size, uncompressed_size);
}

TEST(GarnetFlitTest, DynamicFlitTypeAssignment)
{
    TestMessage msg(MessageSizeType_Response_Data);
    msg.setPayloadSize(16);

    uint32_t ctrl_size =
        Network::MessageSizeType_to_int(MessageSizeType_Control);
    uint32_t total_msg_size = 16 + ctrl_size;
    uint32_t link_width = 16;
    int num_flits = (total_msg_size + link_width - 1) / link_width;

    garnet::RouteInfo route;
    route.vnet = 0;
    route.src_ni = 0;
    route.dest_ni = 1;

    MsgPtr msg_ptr = msg.clone();

    for (int i = 0; i < num_flits; i++) {
        garnet::flit f(1, i, 0, 0, route, num_flits, msg_ptr, total_msg_size,
                       link_width, 0);
        if (num_flits == 1) {
            EXPECT_EQ(f.get_type(), garnet::HEAD_TAIL_);
        } else if (i == 0) {
            EXPECT_EQ(f.get_type(), garnet::HEAD_);
        } else if (i == num_flits - 1) {
            EXPECT_EQ(f.get_type(), garnet::TAIL_);
        } else {
            EXPECT_EQ(f.get_type(), garnet::BODY_);
        }
    }
}

} // namespace ruby
} // namespace gem5
