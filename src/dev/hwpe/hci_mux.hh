#ifndef __DEV_HWPE_HCI_MUX_HH__
#define __DEV_HWPE_HCI_MUX_HH__

#include <memory>
#include <vector>

#include "base/statistics.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/HCIMux.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"

namespace gem5
{

// hci_core_mux_dynamic between HWPEs and one TCDM port (see HCIMux.py).
class HCIMux : public ClockedObject
{
  public:
    PARAMS(HCIMux);
    HCIMux(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;
    DrainState drain() override;
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  private:
    class InPort : public ResponsePort
    {
      public:
        InPort(const std::string &name, HCIMux &mux, unsigned channel);

        const unsigned channel;
        PacketPtr pending = nullptr;   // held until granted
        bool retry = false;            // a request was refused

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;

      private:
        HCIMux &mux;
    };

    class OutPort : public RequestPort
    {
      public:
        OutPort(const std::string &name, HCIMux &mux);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;

      private:
        HCIMux &mux;
    };

    struct Channel : public Packet::SenderState
    {
        unsigned channel;
        Channel(unsigned c) : channel(c) {}
    };

    const unsigned numChannels;
    std::vector<std::unique_ptr<InPort>> inPorts;
    OutPort outPort;

    unsigned rrCounter = 0;
    PacketPtr refused = nullptr;    // sent, refused by the TCDM
    unsigned refusedChannel = 0;
    bool sentNeedsResponse = false; // of the request being sent
    unsigned outstanding = 0;       // granted, response not yet back

    EventFunctionWrapper arbitrateEvent;

    statistics::Scalar granted;
    statistics::Scalar conflicts;

    bool receive(InPort &port, PacketPtr pkt);
    void arbitrate();
    void accepted(unsigned channel, bool needs_response);
    void wake(Tick when);
};

} // namespace gem5

#endif // __DEV_HWPE_HCI_MUX_HH__
