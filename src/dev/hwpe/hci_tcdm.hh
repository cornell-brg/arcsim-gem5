#ifndef __DEV_HWPE_HCI_TCDM_HH__
#define __DEV_HWPE_HCI_TCDM_HH__

#include <memory>
#include <string>
#include <vector>

#include "base/statistics.hh"
#include "mem/abstract_mem.hh"
#include "mem/packet.hh"
#include "mem/qport.hh"
#include "params/HCITcdm.hh"
#include "sim/eventq.hh"

namespace gem5
{

// The HWPE's TCDM: word-interleaved banks shared by the core side and the
// HWPE, arbitrated once per cycle (see HCITcdm.py).
class HCITcdm : public memory::AbstractMemory
{
  public:
    PARAMS(HCITcdm);
    HCITcdm(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;
    DrainState drain() override;
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  private:
    class TcdmPort : public QueuedResponsePort
    {
      public:
        TcdmPort(const std::string &name, HCITcdm &tcdm, bool hwpe);

        const bool hwpe;
        // The request waiting for its grant; the port takes one at a time
        PacketPtr pending = nullptr;
        // The start of the cycle from which the request may compete. On
        // the core side a request wider than a word is made one word
        // (beat) at a time, as the core's AHB transfers: then it is the
        // next beat's, and granted counts the bytes granted so far
        Tick competes = 0;
        unsigned granted = 0;
        // A request was refused while one was pending: ask for it again
        // once the pending one is granted
        bool retry = false;

      protected:
        AddrRangeList getAddrRanges() const override;
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;

      private:
        HCITcdm &tcdm;
        RespPacketQueue queue;
    };

    std::vector<std::unique_ptr<TcdmPort>> corePorts;
    TcdmPort hwpePort;

    const unsigned numBanks;
    const unsigned numBridges;
    const unsigned hwpeWords;
    const unsigned lowPrioMaxStall;
    const Tick window;
    const Tick latency;

    // Consecutive cycles in which both sides requested (ls_stall_ctr)
    unsigned stallCount = 0;
    // The start of the last cycle arbitrated
    Tick lastCycle = MaxTick;
    // Runs window after a clock edge, after everything else of its tick,
    // so that every request of the cycle is in
    EventFunctionWrapper arbitrateEvent;
    // A request that needs no response, deleted once its sender has
    // returned from sendTimingReq
    std::unique_ptr<Packet> pendingDelete;

    struct TcdmStats : public statistics::Group
    {
        TcdmStats(HCITcdm &tcdm);
        statistics::Scalar coreGrants;
        statistics::Scalar hwpeGrants;
        statistics::Scalar coreWaits;
        statistics::Scalar guardCycles;
        statistics::Scalar hwpeConflicts;
        statistics::Scalar refusals;
    } tcdmStats;

    unsigned bankOf(Addr addr) const;
    void validate(PacketPtr pkt, bool hwpe) const;
    bool receive(TcdmPort &port, PacketPtr pkt);
    // The start of the current clock cycle: curTick() if it is on a clock
    // edge, else the last edge before it
    Tick cycleStart() const;
    void arbitrateAt(Tick when);
    // Grant the cycle's requests (see HCITcdm.py)
    void arbitrate();
    // Make port's pending access; answer it delay cycles after this one's
    void grant(TcdmPort &port, unsigned delay);
    bool idle() const;
};

} // namespace gem5

#endif // __DEV_HWPE_HCI_TCDM_HH__
