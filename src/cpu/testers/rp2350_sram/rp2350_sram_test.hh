#ifndef __CPU_TESTERS_RP2350_SRAM_TEST_HH__
#define __CPU_TESTERS_RP2350_SRAM_TEST_HH__

#include <deque>
#include <memory>
#include <vector>
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/RP2350SRAMTest.hh"
#include "sim/clocked_object.hh"

namespace gem5
{
class RP2350SRAMTest : public ClockedObject
{
    struct Expected : Packet::SenderState
    {
        std::vector<uint8_t> data;
        Tick due = 0;
    };
    class TestPort : public RequestPort
    {
      public:
        RP2350SRAMTest &test;
        std::deque<PacketPtr> pending;
        TestPort(RP2350SRAMTest &test, PortID id);
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override {}
        void send();
    };
    std::vector<std::unique_ptr<TestPort>> ports;
    const std::string scenario;
    const Tick sramWindow;
    const Tick sramLatency;
    RequestorID requestor;
    EventFunctionWrapper stepEvent;
    EventFunctionWrapper retryEvent;
    unsigned phase = 0;
    unsigned received = 0;
    bool rejected = false;
    Tick start = 0;
    PacketPtr packet(Addr addr, unsigned size, bool write,
                     const std::vector<uint8_t> &data, Tick due = 0);
    void step();
    bool response(PacketPtr pkt, PortID id);
  public:
    explicit RP2350SRAMTest(const RP2350SRAMTestParams &p);
    Port &getPort(const std::string &name, PortID id) override;
    void startup() override;
};
}
#endif
