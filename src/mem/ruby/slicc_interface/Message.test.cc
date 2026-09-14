/*
 * Unit tests for dynamic Message payload sizing and flit count determination.
 */

#include <gtest/gtest.h>

#include "mem/ruby/network/garnet/flit.hh"
#include "mem/ruby/slicc_interface/Message.hh"
#include "mem/ruby/system/RubySystem.hh"

using namespace gem5;
using namespace ruby;
using namespace ruby::garnet;

int RubySystem::MachineType_base_count(const MachineType& t) { return 1; }
int RubySystem::MachineType_base_number(const MachineType& t) { return 0; }

class DummyMessage : public Message
{
  private:
    int payload_size;

  public:
    DummyMessage(int size = -1)
        : Message(0, 64, nullptr), payload_size(size)
    { }

    MsgPtr clone() const override
    {
        return MsgPtr(new DummyMessage(*this));
    }

    void print(std::ostream& out) const override
    {
        out << "[DummyMessage payload=" << payload_size << "]";
    }

    int getPayloadSizeInBytes() const override
    {
        return payload_size;
    }
};

TEST(MessageTest, DefaultPayloadSizeInBytes)
{
    // Base Message class default returns -1
    DummyMessage msg_default(-1);
    EXPECT_EQ(msg_default.getPayloadSizeInBytes(), -1);
}

TEST(MessageTest, CompressedPayloadSizeInBytes)
{
    DummyMessage msg_zero(0);
    EXPECT_EQ(msg_zero.getPayloadSizeInBytes(), 0);

    DummyMessage msg_32(32);
    EXPECT_EQ(msg_32.getPayloadSizeInBytes(), 32);
}

TEST(MessageTest, DynamicFlitCountAndType)
{
    uint32_t bit_width = 8; // 64 bits = 8 bytes per flit
    int control_bytes = 8;  // control header overhead

    // Test 1: Zero block (0-byte payload)
    int payload_bytes_zero = 0;
    int msg_size_zero = control_bytes + payload_bytes_zero; // 8 bytes
    int num_flits_zero = (msg_size_zero + bit_width - 1) / bit_width; // 1 flit
    EXPECT_EQ(num_flits_zero, 1);

    RouteInfo route;
    flit f_zero(0, 0, 0, 0, route, num_flits_zero, nullptr, msg_size_zero, bit_width, 0);
    EXPECT_EQ(f_zero.get_type(), HEAD_TAIL_);

    // Test 2: 2:1 Compressed block (32-byte payload)
    int payload_bytes_32 = 32;
    int msg_size_32 = control_bytes + payload_bytes_32; // 40 bytes
    int num_flits_32 = (msg_size_32 + bit_width - 1) / bit_width; // 5 flits
    EXPECT_EQ(num_flits_32, 5);

    flit f_head(1, 0, 0, 0, route, num_flits_32, nullptr, msg_size_32, bit_width, 0);
    EXPECT_EQ(f_head.get_type(), HEAD_);

    flit f_body(1, 2, 0, 0, route, num_flits_32, nullptr, msg_size_32, bit_width, 0);
    EXPECT_EQ(f_body.get_type(), BODY_);

    flit f_tail(1, 4, 0, 0, route, num_flits_32, nullptr, msg_size_32, bit_width, 0);
    EXPECT_EQ(f_tail.get_type(), TAIL_);

    // Test 3: Uncompressed block (64-byte payload)
    int payload_bytes_64 = 64;
    int msg_size_64 = control_bytes + payload_bytes_64; // 72 bytes
    int num_flits_64 = (msg_size_64 + bit_width - 1) / bit_width; // 9 flits
    EXPECT_EQ(num_flits_64, 9);
    EXPECT_LT(num_flits_32, num_flits_64);
}
