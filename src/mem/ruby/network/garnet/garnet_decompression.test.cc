/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Decompression-aware Garnet Crossbar Switch Arbitration Unit Tests
 */

#include <gtest/gtest.h>

#include "sim/cur_tick.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "mem/ruby/network/garnet/CommonTypes.hh"
#include "mem/ruby/network/garnet/OutputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/network/garnet/flit.hh"

namespace gem5
{
namespace ruby
{
int RubySystem::MachineType_base_count(const MachineType &obj) { return 0; }
int RubySystem::MachineType_base_number(const MachineType &obj) { return 0; }
MachineType MachineType_from_base_level(int level) { return MachineType_NUM; }
int MachineType_base_level(const MachineType &obj) { return 0; }
MachineType &operator++(MachineType &type)
{
    type = static_cast<MachineType>(static_cast<int>(type) + 1);
    return type;
}

namespace garnet
{
std::string Router::getPortDirectionName(PortDirection direction) { return direction; }

class GarnetDecompressionTest : public ::testing::Test
{
  protected:
    Tick mockTick;

    void SetUp() override
    {
        mockTick = 100;
        Gem5Internal::_curTickPtr = &mockTick;
    }
};

TEST_F(GarnetDecompressionTest, OutputUnitDecompressionBusyStatus)
{
    // Test boolean and tick-based decompression busy status
    OutputUnit out_unit(0, "Local", nullptr, 1);

    EXPECT_FALSE(out_unit.is_decompression_busy());

    out_unit.set_decompression_busy(true);
    EXPECT_TRUE(out_unit.is_decompression_busy());

    out_unit.set_decompression_busy(false);
    EXPECT_FALSE(out_unit.is_decompression_busy());

    // Set finish time in the future
    out_unit.set_decompression_busy_until(150);
    EXPECT_TRUE(out_unit.is_decompression_busy());

    // Advance mock tick past finish time
    mockTick = 160;
    EXPECT_FALSE(out_unit.is_decompression_busy());
}

TEST_F(GarnetDecompressionTest, FlitDemandRequestClassification)
{
    RouteInfo route;
    route.vnet = 0;
    flit req_flit(1, 0, 0, 0, route, 1, nullptr, 8, 8, mockTick);
    EXPECT_TRUE(req_flit.is_demand_request());

    route.vnet = 1;
    flit resp_flit(2, 0, 0, 1, route, 1, nullptr, 8, 8, mockTick);
    EXPECT_FALSE(resp_flit.is_demand_request());

    resp_flit.set_demand_request(true);
    EXPECT_TRUE(resp_flit.is_demand_request());
}

TEST_F(GarnetDecompressionTest, DecompressionAwarePriorityEvaluation)
{
    OutputUnit out_unit_busy(0, "Local", nullptr, 1);
    OutputUnit out_unit_idle(1, "Local", nullptr, 1);

    out_unit_busy.set_decompression_busy(true);
    out_unit_idle.set_decompression_busy(false);

    RouteInfo route;
    route.vnet = 1; // writeback / response vnet
    flit wb_flit(1, 0, 0, 1, route, 1, nullptr, 8, 8, mockTick);
    EXPECT_FALSE(wb_flit.is_demand_request());

    route.vnet = 0; // demand request vnet
    flit req_flit(2, 0, 0, 0, route, 1, nullptr, 8, 8, mockTick);
    EXPECT_TRUE(req_flit.is_demand_request());

    // Helper lambda mirroring SwitchAllocator priority evaluation
    auto eval_priority = [](OutputUnit *out_unit, flit *t_flit) {
        bool decomp_busy = out_unit ? out_unit->is_decompression_busy() : false;
        int prio = 1;
        if (decomp_busy) {
            prio = 0; // Deprioritize requests blocked on decompression
        } else if (t_flit && t_flit->is_demand_request()) {
            prio = 2; // Priority boost for unblocked demand requests
        }
        return prio;
    };

    // Scenario 1: Writeback targeting decompression-busy endpoint -> priority 0 (deprioritized)
    EXPECT_EQ(eval_priority(&out_unit_busy, &wb_flit), 0);

    // Scenario 1: Demand request targeting unblocked endpoint -> priority 2 (boosted)
    EXPECT_EQ(eval_priority(&out_unit_idle, &req_flit), 2);

    // Bypassing logic check: demand request priority (2) > blocked writeback priority (0)
    EXPECT_GT(eval_priority(&out_unit_idle, &req_flit), eval_priority(&out_unit_busy, &wb_flit));

    // Scenario 2: Decompression completes at endpoint -> writeback resumes normal arbitration priority (1)
    out_unit_busy.set_decompression_busy(false);
    EXPECT_EQ(eval_priority(&out_unit_busy, &wb_flit), 1);
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
