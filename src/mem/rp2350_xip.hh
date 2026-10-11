#ifndef __MEM_RP2350_XIP_HH__
#define __MEM_RP2350_XIP_HH__

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <vector>

#include "base/random.hh"
#include "base/statistics.hh"
#include "mem/abstract_mem.hh"
#include "mem/qport.hh"
#include "params/RP2350XIP.hh"
#include "sim/eventq.hh"

namespace gem5::memory
{

class RP2350XIP : public AbstractMemory
{
  private:
    static constexpr unsigned LineBytes = 8;
    static constexpr unsigned NumSets = 1024;
    static constexpr unsigned NumWays = 2;
    static constexpr unsigned NumBanks = 2;

    // An access on its way through the cache: the lines it still needs,
    // and the earliest tick its answer may leave.
    struct Access
    {
        PacketPtr pkt;
        Addr nextLine = 0;
        Addr endLine = 0;
        Tick due = 0;
        // A write to the cache maintenance window
        bool maintenance = false;
        // The bus manager it came from
        RequestorID requestor = 0;
        // It has had to wait for a bank or for flash
        bool waited = false;
        // Words of its present line still to come from a bank that is
        // serving those that waited; 0 before its first
        unsigned beatsLeft = 0;
    };
    using AccessPtr = std::shared_ptr<Access>;

    class XipPort : public QueuedResponsePort
    {
      public:
        XipPort(const std::string &name, RP2350XIP &xip);

      protected:
        RP2350XIP &xip;
        RespPacketQueue queue;

        AddrRangeList getAddrRanges() const override;
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
    };

    struct Way
    {
        bool valid = false;
        Addr line = 0;
    };

    // One of the cache's two banks: the lines of even or of odd line
    // address.
    struct Bank
    {
        // Waiting for a line from flash, for `owner`
        bool busy = false;
        AccessPtr owner;
        Addr missLine = 0;
        // Accesses that found the bank busy, oldest first
        std::deque<AccessPtr> waiting;
        // The cycle in which the bank last served a waiting access, and
        // the manager that access came from
        Tick servedCycle = MaxTick;
        RequestorID lastRequestor = 0;
    };

    // A bus manager makes one access at a time: while one of its accesses
    // waits, its later ones are held in order.
    struct Manager
    {
        bool waiting = false;
        Tick freeAt = 0;
        std::deque<AccessPtr> held;
    };

    XipPort port;
    const AddrRange maintenanceRange;
    const Cycles missCycles;
    const Cycles chainedCycles;
    const Cycles reselectCycles;
    const Cycles cooldownCycles;
    const Addr pageBreak;

    std::array<std::array<Way, NumWays>, NumSets> sets;
    std::array<Bank, NumBanks> banks;
    std::map<RequestorID, Manager> managers;
    mutable Random::RandomPtr rng = Random::genRandom();

    // The QMI: the banks whose lines are to be read, oldest first; the one
    // being read is at the front once `qmiBusy`.
    std::deque<unsigned> qmiQueue;
    bool qmiBusy = false;
    // The end of the last transfer and the line after the one it read;
    // MaxTick before any transfer
    Tick qmiLastEnd = MaxTick;
    Addr qmiNextLine = 0;

    unsigned outstanding = 0;
    EventFunctionWrapper qmiEvent;
    EventFunctionWrapper releaseEvent;
    std::array<EventFunctionWrapper, NumBanks> serveEvents;
    std::unique_ptr<Packet> pendingDelete;

    struct XIPStats : public statistics::Group
    {
        statistics::Scalar hits;
        statistics::Scalar misses;
        statistics::Scalar chained;
        statistics::Scalar bankWaits;
        statistics::Scalar invalidations;
        XIPStats(RP2350XIP &xip);
    } xipStats;

    static Addr lineOf(Addr addr) { return addr & ~Addr(LineBytes - 1); }
    static unsigned bankOf(Addr line) { return (line / LineBytes) % NumBanks; }
    static unsigned setOf(Addr line) { return (line / LineBytes) % NumSets; }
    Way *lookup(Addr line);
    void fill(Addr line);

    bool receive(PacketPtr pkt);
    void maintain(PacketPtr pkt);
    // The 32-bit words of `line` the access covers
    static unsigned wordsIn(const Access &access, Addr line);
    // Takes the access through the lines it still needs, until one must
    // wait.
    void advance(const AccessPtr &access);
    void startMiss(unsigned bank, const AccessPtr &access, Addr line);
    void markWaited(const AccessPtr &access);
    void respond(const AccessPtr &access);
    void release();
    void startQmi(Tick start);
    void finishQmi();
    // The clock edge after now
    Tick nextCycle() const;
    void serve(unsigned bank);

  public:
    explicit RP2350XIP(const RP2350XIPParams &p);
    Port &getPort(const std::string &name, PortID id = InvalidPortID) override;
    void init() override;
    DrainState drain() override;
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;
};

} // namespace gem5::memory
#endif
