/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Dynamic Flit Truncation Unit Test for Garnet Network Interface
 */

#include <gtest/gtest.h>

#include "base/intmath.hh"
#include "mem/ruby/network/garnet/CommonTypes.hh"
#include "mem/ruby/network/garnet/flit.hh"
#include "mem/ruby/slicc_interface/Message.hh"
#include "mem/ruby/system/RubySystem.hh"

namespace gem5 {
namespace ruby {

int MachineType_base_level(const MachineType& machine) { return 0; }
MachineType MachineType_from_base_level(int level) { return MachineType_NUM; }
MachineType& operator++(MachineType& m) { return m; }

int RubySystem::MachineType_base_count(const MachineType& machine) { return 1; }
int RubySystem::MachineType_base_number(const MachineType& machine) { return 0; }

} // namespace ruby
} // namespace gem5

using namespace gem5;
using namespace gem5::ruby;
using namespace gem5::ruby::garnet;

class DummyTestMessage : public Message
{
  public:
    DummyTestMessage(MessageSizeType size_type = MessageSizeType_Data)
        : Message(0, 64, nullptr), m_size_type(size_type)
    {}

    MsgPtr clone() const override {
        return std::make_shared<DummyTestMessage>(*this);
    }

    void print(std::ostream& out) const override {
        out << "[DummyTestMessage]";
    }

    const MessageSizeType& getMessageSize() const override {
        return m_size_type;
    }

    MessageSizeType& getMessageSize() override {
        return m_size_type;
    }

  private:
    MessageSizeType m_size_type;
};

TEST(FlitTruncationTest, MessageCompressedPayloadSize)
{
    DummyTestMessage msg(MessageSizeType_Data);
    EXPECT_EQ(msg.getCompressedPayloadSize(), -1);
    EXPECT_FALSE(msg.isPayloadCompressed());

    msg.setCompressedPayloadSize(16);
    EXPECT_EQ(msg.getCompressedPayloadSize(), 16);
    EXPECT_TRUE(msg.isPayloadCompressed());

    MsgPtr cloned_msg = msg.clone();
    EXPECT_EQ(cloned_msg->getCompressedPayloadSize(), 16);
    EXPECT_TRUE(cloned_msg->isPayloadCompressed());
}

TEST(FlitTruncationTest, FlitSequenceTypesAndSize)
{
    // Test 5-flit sequence (uncompressed 72 bytes, 16-byte link bitwidth)
    int uncomp_size = 72;
    uint32_t bit_width = 16;
    int num_flits_5 = divCeil(uncomp_size, bit_width);
    EXPECT_EQ(num_flits_5, 5);

    RouteInfo route;
    route.vnet = 0;
    route.src_ni = 0;
    route.dest_ni = 1;

    MsgPtr msg = std::make_shared<DummyTestMessage>(MessageSizeType_Data);

    flit f0(1, 0, 0, 0, route, num_flits_5, msg, uncomp_size, bit_width, 0);
    flit f1(1, 1, 0, 0, route, num_flits_5, msg, uncomp_size, bit_width, 0);
    flit f4(1, 4, 0, 0, route, num_flits_5, msg, uncomp_size, bit_width, 0);

    EXPECT_EQ(f0.get_type(), HEAD_);
    EXPECT_EQ(f1.get_type(), BODY_);
    EXPECT_EQ(f4.get_type(), TAIL_);

    // Test 2-flit sequence (truncated to 24 bytes: 8 header + 16 compressed payload)
    int trunc_size_24 = 24;
    int num_flits_2 = divCeil(trunc_size_24, bit_width);
    EXPECT_EQ(num_flits_2, 2);

    flit tf0(2, 0, 0, 0, route, num_flits_2, msg, trunc_size_24, bit_width, 0);
    flit tf1(2, 1, 0, 0, route, num_flits_2, msg, trunc_size_24, bit_width, 0);

    EXPECT_EQ(tf0.get_type(), HEAD_);
    EXPECT_EQ(tf1.get_type(), TAIL_);

    // Test 1-flit sequence (zero block payload: 8 header + 0 compressed payload)
    int trunc_size_8 = 8;
    int num_flits_1 = divCeil(trunc_size_8, bit_width);
    EXPECT_EQ(num_flits_1, 1);

    flit zf0(3, 0, 0, 0, route, num_flits_1, msg, trunc_size_8, bit_width, 0);

    EXPECT_EQ(zf0.get_type(), HEAD_TAIL_);
}

TEST(FlitTruncationTest, FlitisizeMessageSizeCalculation)
{
    // Helper lambda implementing NetworkInterface dynamic message size logic
    auto calc_msg_size = [](Message* net_msg_ptr, int uncomp_data_size, int header_size) {
        int msg_byte_size = uncomp_data_size;
        if (net_msg_ptr->getCompressedPayloadSize() >= 0 && msg_byte_size > header_size) {
            int uncomp_payload = msg_byte_size - header_size;
            int comp_payload = std::min(net_msg_ptr->getCompressedPayloadSize(), uncomp_payload);
            msg_byte_size = header_size + comp_payload;
        }
        return msg_byte_size;
    };

    uint32_t bit_width = 16;
    int data_msg_uncomp_bytes = 72; // 8 header + 64 data
    int control_msg_bytes = 8;     // 8 header

    // 1. Default / Uncompressed Data Message (-1)
    DummyTestMessage uncomp_data_msg(MessageSizeType_Data);
    int size1 = calc_msg_size(&uncomp_data_msg, data_msg_uncomp_bytes, control_msg_bytes);
    EXPECT_EQ(size1, 72);
    EXPECT_EQ(divCeil(size1, bit_width), 5);

    // 2. Data Message compressed to 16 bytes
    DummyTestMessage comp16_data_msg(MessageSizeType_Data);
    comp16_data_msg.setCompressedPayloadSize(16);
    int size2 = calc_msg_size(&comp16_data_msg, data_msg_uncomp_bytes, control_msg_bytes);
    EXPECT_EQ(size2, 24); // 8 header + 16 payload
    EXPECT_EQ(divCeil(size2, bit_width), 2);

    // 3. Data Message compressed to 0 bytes (Zero block)
    DummyTestMessage comp0_data_msg(MessageSizeType_Data);
    comp0_data_msg.setCompressedPayloadSize(0);
    int size3 = calc_msg_size(&comp0_data_msg, data_msg_uncomp_bytes, control_msg_bytes);
    EXPECT_EQ(size3, 8); // 8 header + 0 payload
    EXPECT_EQ(divCeil(size3, bit_width), 1);

    // 4. Data Message with compressed size larger than uncompressed payload (e.g. 100 bytes)
    DummyTestMessage comp_oversized_msg(MessageSizeType_Data);
    comp_oversized_msg.setCompressedPayloadSize(100);
    int size4 = calc_msg_size(&comp_oversized_msg, data_msg_uncomp_bytes, control_msg_bytes);
    EXPECT_EQ(size4, 72); // Clamped to 8 + 64
    EXPECT_EQ(divCeil(size4, bit_width), 5);

    // 5. Control Message (uncomp size <= header size)
    DummyTestMessage ctrl_msg(MessageSizeType_Control);
    ctrl_msg.setCompressedPayloadSize(16); // Setting compressed payload size on control msg
    int size5 = calc_msg_size(&ctrl_msg, control_msg_bytes, control_msg_bytes);
    EXPECT_EQ(size5, 8); // Bypasses payload truncation
    EXPECT_EQ(divCeil(size5, bit_width), 1);
}
