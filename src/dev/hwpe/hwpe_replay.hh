#ifndef __DEV_HWPE_HWPE_REPLAY_HH__
#define __DEV_HWPE_HWPE_REPLAY_HH__

#include <cstdint>
#include <string>
#include <vector>

#include "dev/intpin.hh"
#include "mem/port.hh"
#include "params/HWPEReplay.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"

namespace gem5
{

// Plays an RTL testbench's HWPE jobs into an HWPE model (see
// HWPEReplay.py).
class HWPEReplay : public ClockedObject
{
  public:
    PARAMS(HWPEReplay);
    HWPEReplay(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void startup() override;

    void raiseInterruptPin(int number);
    void lowerInterruptPin(int number) {}

  private:
    class ReqPort : public RequestPort
    {
      public:
        ReqPort(const std::string &name) : RequestPort(name) {}

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override {}
    };

    // A register access: dones before it in the trace and its cycle after
    // the last of them (or after reset)
    struct Access
    {
        unsigned dones;
        uint64_t offset;
        bool write;
        Addr reg;
        uint32_t data;
    };
    // Memory to load before a job: what the RTL engine read between done
    // n - 1 and done n, where nothing it wrote in that span preceded it
    struct Word
    {
        Addr addr;
        std::vector<uint8_t> data;
    };

    const Addr hwpeBase;
    const Addr memBase;
    ReqPort hwpePort;
    ReqPort memPort;
    IntSinkPin<HWPEReplay> irq;

    std::vector<Access> accesses;
    std::vector<std::vector<Word>> loads;   // [dones before]
    unsigned dones = 0;                     // in the replay
    unsigned totalDones = 0;                // in the trace
    size_t nextAccess = 0;
    Tick lastDone = 0;

    EventFunctionWrapper accessEvent;

    void parse(const std::string &path);
    void load(unsigned n);
    void schedule_next();
    void issue();
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_REPLAY_HH__
