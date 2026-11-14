#ifndef __MEM_RP2350_XIP_HH__
#define __MEM_RP2350_XIP_HH__

#include <array>
#include <deque>
#include <memory>

#include "base/statistics.hh"
#include "mem/abstract_mem.hh"
#include "mem/port.hh"
#include "params/RP2350XIP.hh"
#include "sim/eventq.hh"

namespace gem5::memory
{

class RP2350XIP : public AbstractMemory
{
    struct Line {
        Addr tag = 0;
        Tick ready = 0;
        bool valid = false;
        bool pinned = false;
        bool dirty = false;
        std::array<uint8_t, 8> data{};
    };
    class XipPort : public ResponsePort {
        RP2350XIP &owner;
      public:
        XipPort(const std::string &name, RP2350XIP &o)
            : ResponsePort(name), owner(o) {}
        AddrRangeList getAddrRanges() const override;
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    } port;
    struct Response { PacketPtr pkt; Tick ready; };
    std::deque<Response> responses;
    bool retryRequest = false;
    bool retryResponse = false;
    std::unique_ptr<Packet> pendingDelete;
    EventFunctionWrapper responseEvent;
    std::array<std::array<Line, 2>, 1024> sets;
    std::array<Tick, 2> bankFree{};
    Tick flashFree = 0;
    Addr flashNext = 0;
    bool flashSelected = false;
    uint32_t randomState;
    uint32_t control = 0x83;
    uint32_t hits = 0;
    uint32_t accesses = 0;
    const unsigned hitCycles, clkdiv, prefix, address, suffix, dummy;
    const unsigned dataWidth, minDeselect, sampleCycles, cooldown, pagebreak;
    struct XipStats : public statistics::Group {
        statistics::Scalar hits, misses, uncached, continuations, commands;
        statistics::Scalar maintenance, bankWaitCycles;
        XipStats(RP2350XIP &m);
    } stats;

    Tick service(PacketPtr pkt, bool timing);
    Tick flashRead(Addr address, unsigned bytes, Tick when);
    bool maintain(Addr address, Tick when);
    unsigned victim();
    void functional(PacketPtr pkt);
    bool receive(PacketPtr pkt);
    void respond();
    void queueResponse(PacketPtr pkt, Tick ready);
    static void increment(uint32_t &counter);

  public:
    explicit RP2350XIP(const RP2350XIPParams &p);
    Port &getPort(const std::string &name, PortID id = InvalidPortID) override;
    void init() override;
    DrainState drain() override;
    uint32_t readControl(Addr offset) const;
    void writeControl(Addr offset, uint32_t value, unsigned atomicOp);
};

} // namespace gem5::memory
#endif
