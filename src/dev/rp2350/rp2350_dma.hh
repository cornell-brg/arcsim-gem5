#ifndef __DEV_RP2350_RP2350_DMA_HH__
#define __DEV_RP2350_RP2350_DMA_HH__

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <set>
#include <vector>

#include "base/statistics.hh"
#include "dev/intpin.hh"
#include "dev/io_device.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/RP2350DMA.hh"
#include "sim/eventq.hh"

namespace gem5
{

// The RP2350's DMA controller (see RP2350DMA.py).
class RP2350DMA : public BasicPioDevice
{
  public:
    PARAMS(RP2350DMA);
    RP2350DMA(const Params &p);

    // `count` pulses on data request line `line` (a DREQ number of the
    // datasheet's table 1146): each lets the channel whose TREQ_SEL selects
    // the line make one transfer.
    void dreq(unsigned line, unsigned count = 1);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;
    DrainState drain() override;
    void drainResume() override;
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

  private:
    static constexpr unsigned NumChannels = 16;
    static constexpr unsigned NumIrqs = 4;
    // Transfers that have been read and not yet written. The chip's FIFO
    // depths are not documented; this lets an unpaced copy between
    // single-cycle memories run at the one transfer per cycle the datasheet
    // gives.
    static constexpr unsigned DataFifoDepth = 8;
    // The DREQ counter is six bits and saturates.
    static constexpr unsigned MaxCredit = 63;

    // Register offsets (RP2350 datasheet, DMA register list). The block
    // repeats at +0x1000 (XOR), +0x2000 (SET) and +0x3000 (CLR).
    enum : Addr
    {
        CHANNELS_END = 0x400,  // 16 channels of 0x40: four alias rows each
        INTR = 0x400,
        INTE0 = 0x404,         // then INTF0, INTS0; IRQ n is 0x10 * n on
        INTF0 = 0x408,
        INTS0 = 0x40C,
        IRQS_END = 0x440,
        MULTI_CHAN_TRIGGER = 0x450,
        CHAN_ABORT = 0x464,
        N_CHANNELS = 0x468,
        BLOCK_SIZE = 0x1000,
    };

    // CTRL fields
    enum : uint32_t
    {
        CTRL_EN = 1u << 0,
        CTRL_HIGH_PRIORITY = 1u << 1,
        CTRL_DATA_SIZE_LSB = 2,
        CTRL_INCR_READ = 1u << 4,
        CTRL_INCR_READ_REV = 1u << 5,
        CTRL_INCR_WRITE = 1u << 6,
        CTRL_INCR_WRITE_REV = 1u << 7,
        CTRL_RING_SIZE_LSB = 8,
        CTRL_RING_SEL = 1u << 12,
        CTRL_CHAIN_TO_LSB = 13,
        CTRL_TREQ_SEL_LSB = 17,
        CTRL_IRQ_QUIET = 1u << 23,
        CTRL_BSWAP = 1u << 24,
        CTRL_SNIFF_EN = 1u << 25,
        CTRL_BUSY = 1u << 26,
        // What software can write: everything below BUSY
        CTRL_WRITABLE = CTRL_BUSY - 1,
        CTRL_UNMODELLED = CTRL_HIGH_PRIORITY | CTRL_INCR_READ_REV |
                          CTRL_INCR_WRITE_REV | CTRL_BSWAP | CTRL_SNIFF_EN,
    };

    // TREQ_SEL values that are not a peripheral's line
    static constexpr unsigned TreqTimer0 = 0x3B;
    static constexpr unsigned TreqPermanent = 0x3F;

    // TRANS_COUNT: a mode in the top four bits, the count below
    static constexpr uint32_t CountMask = 0x0FFFFFFF;
    static constexpr unsigned ModeTriggerSelf = 0x1;
    static constexpr unsigned ModeEndless = 0xF;

    // A channel's four registers, in alias 0's order
    enum class Reg { ReadAddr, WriteAddr, TransCount, Ctrl };
    // How a write combines with the register: plain, or an atomic alias
    enum class Op { Put, Xor, Set, Clr };

    struct Channel
    {
        uint32_t readAddr = 0;
        uint32_t writeAddr = 0;
        uint32_t countReload = 0;   // TRANS_COUNT as last written
        uint32_t count = 0;         // transfers left in this sequence
        uint32_t ctrl = 0;
        bool busy = false;
        bool aborting = false;
        unsigned credit = 0;        // DREQ pulses not yet used
        unsigned inFlight = 0;      // transfers started and not yet written

        unsigned treq() const { return (ctrl >> CTRL_TREQ_SEL_LSB) & 0x3F; }
        unsigned chainTo() const { return (ctrl >> CTRL_CHAIN_TO_LSB) & 0xF; }
        unsigned mode() const { return countReload >> 28; }
    };

    // One transfer on its way from the read manager to the write manager.
    struct Transfer : public Packet::SenderState
    {
        unsigned channel;
        Addr readAddr;
        Addr writeAddr;
        unsigned size;
        uint8_t data[4] = {0, 0, 0, 0};
        bool haveData = false;
        Tick dataTick = 0;
    };

    class ManagerPort : public RequestPort
    {
      public:
        ManagerPort(const std::string &name, RP2350DMA &dma, bool is_read);
        // Send, or hold the packet until the bus takes it.
        void send(PacketPtr pkt);
        bool waiting() const { return held != nullptr; }

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override {}

      private:
        RP2350DMA &dma;
        const bool isRead;
        PacketPtr held = nullptr;
    };

    ManagerPort readPort;
    ManagerPort writePort;
    std::vector<std::unique_ptr<IntSourcePin<RP2350DMA>>> irqPins;
    RequestorID requestorId;

    std::array<Channel, NumChannels> channels;
    uint32_t intr = 0;                      // one bit per channel
    uint32_t inte[NumIrqs] = {};
    uint32_t intf[NumIrqs] = {};
    // Channels chained to, to trigger at the next clock edge.
    uint32_t pendingTriggers = 0;
    unsigned nextChannel = 0;               // round-robin position
    // Transfers read or being read, oldest first.
    std::deque<Transfer *> dataFifo;
    unsigned writesInFlight = 0;
    unsigned readsInFlight = 0;
    // The start of the clock cycle the last read left in
    Tick lastReadCycle = MaxTick;
    const Tick window;

    EventFunctionWrapper tickEvent;

    // What has been reported as not modelled: register offsets, and CTRL
    // bits and pacing timers by a number above any offset.
    std::set<Addr> warned;

    struct DMAStats : public statistics::Group
    {
        statistics::Vector transfers;
        DMAStats(RP2350DMA &dma);
    } stats;

    static Reg aliasRegister(Addr offset);
    uint32_t readRegister(Addr offset);
    void writeRegister(Addr offset, uint32_t value, Op op);
    uint32_t readChannel(unsigned index, Reg reg) const;
    void writeChannel(unsigned index, Reg reg, uint32_t value);
    void unmodelled(Addr offset, bool is_write);
    void unmodelledOnce(Addr key, const char *what);

    void trigger(unsigned index);
    void abort(unsigned index);
    void finish(unsigned index);
    bool requesting(const Channel &channel) const;
    Transfer *start(unsigned index);
    void recvResponse(PacketPtr pkt, bool is_read);
    void raise(uint32_t channel_bits);
    void updateIrqs();

    void tick();
    void startRead();
    void wake();
    bool idle() const;
};

} // namespace gem5

#endif // __DEV_RP2350_RP2350_DMA_HH__
