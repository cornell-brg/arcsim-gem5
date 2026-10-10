#include "dev/rp2350/rp2350_dma.hh"

#include <algorithm>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/RP2350DMA.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{

RP2350DMA::DMAStats::DMAStats(RP2350DMA &dma)
    : statistics::Group(&dma),
      ADD_STAT(transfers, statistics::units::Count::get(),
               "Transfers started, per channel")
{
    transfers.init(NumChannels).flags(statistics::total);
}

RP2350DMA::ManagerPort::ManagerPort(const std::string &name, RP2350DMA &_dma,
                                    bool is_read)
    : RequestPort(name), dma(_dma), isRead(is_read)
{
}

void
RP2350DMA::ManagerPort::send(PacketPtr pkt)
{
    if (!sendTimingReq(pkt))
        held = pkt;
}

bool
RP2350DMA::ManagerPort::recvTimingResp(PacketPtr pkt)
{
    dma.recvResponse(pkt, isRead);
    return true;
}

void
RP2350DMA::ManagerPort::recvReqRetry()
{
    PacketPtr pkt = held;
    held = nullptr;
    send(pkt);
    dma.wake();
}

RP2350DMA::RP2350DMA(const Params &p)
    : BasicPioDevice(p, 4 * BLOCK_SIZE),
      readPort(name() + ".read_port", *this, true),
      writePort(name() + ".write_port", *this, false),
      requestorId(p.system->getRequestorId(this)),
      tickEvent([this] { tick(); }, name() + ".tick"),
      stats(*this)
{
    fatal_if(p.port_irq_connection_count > NumIrqs,
             "RP2350DMA has %d interrupt outputs", NumIrqs);
    for (unsigned n = 0; n < p.port_irq_connection_count; ++n) {
        irqPins.emplace_back(std::make_unique<IntSourcePin<RP2350DMA>>(
            csprintf("%s.irq[%d]", name(), n), n, this));
    }
}

Port &
RP2350DMA::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "read_port")
        return readPort;
    if (if_name == "write_port")
        return writePort;
    if (if_name == "irq") {
        fatal_if(idx < 0 || static_cast<size_t>(idx) >= irqPins.size(),
                 "RP2350DMA: no interrupt output %d", idx);
        return *irqPins[idx];
    }
    return BasicPioDevice::getPort(if_name, idx);
}

void
RP2350DMA::init()
{
    BasicPioDevice::init();
    fatal_if(!readPort.isConnected() || !writePort.isConnected(),
             "RP2350DMA: connect read_port and write_port");
}

// ---- registers --------------------------------------------------------

void
RP2350DMA::unmodelledOnce(Addr key, const char *what)
{
    if (warned.insert(key).second)
        warn("RP2350DMA: %s\n", what);
}

void
RP2350DMA::unmodelled(Addr offset, bool is_write)
{
    if (warned.insert(offset).second)
        warn("RP2350DMA: register %#x is not modelled (%s); it reads 0 and "
             "ignores writes\n", offset, is_write ? "write" : "read");
}

uint32_t
RP2350DMA::readChannel(unsigned index, Reg reg) const
{
    const Channel &c = channels[index];
    switch (reg) {
      case Reg::ReadAddr: return c.readAddr;
      case Reg::WriteAddr: return c.writeAddr;
      // The transfers left, under the mode last written
      case Reg::TransCount: return (c.countReload & ~CountMask) | c.count;
      case Reg::Ctrl: return c.ctrl | (c.busy ? CTRL_BUSY : 0);
    }
    panic("RP2350DMA: bad channel register");
}

void
RP2350DMA::writeChannel(unsigned index, Reg reg, uint32_t value)
{
    Channel &c = channels[index];
    switch (reg) {
      case Reg::ReadAddr:
        c.readAddr = value;
        break;
      case Reg::WriteAddr:
        c.writeAddr = value;
        break;
      case Reg::TransCount:
        // Takes effect at the next trigger
        c.countReload = value;
        break;
      case Reg::Ctrl: {
        const unsigned old_treq = c.treq();
        c.ctrl = value & CTRL_WRITABLE;
        if (c.treq() != old_treq)
            c.credit = 0;
        if (c.ctrl & CTRL_UNMODELLED) {
            unmodelledOnce(BLOCK_SIZE, "CTRL's HIGH_PRIORITY, INCR_READ_REV, "
                "INCR_WRITE_REV, BSWAP and SNIFF_EN are not modelled; a "
                "channel runs as if they were 0");
        }
        if (c.treq() >= TreqTimer0 && c.treq() < TreqPermanent) {
            unmodelledOnce(BLOCK_SIZE + 1, "the pacing timers are not "
                "modelled; a channel paced by one never transfers");
        }
        break;
      }
    }
    DPRINTF(RP2350DMA, "ch%d read %#x write %#x count %#x ctrl %#x\n", index,
            c.readAddr, c.writeAddr, c.countReload, c.ctrl);
    wake();
}

// Which of a channel's registers is at an offset: four rows of four, alias
// 0 in the registers' own order, each later row starting with CTRL and
// ending with a different one of the other three.
RP2350DMA::Reg
RP2350DMA::aliasRegister(Addr offset)
{
    static const Reg alias[4][4] = {
        {Reg::ReadAddr, Reg::WriteAddr, Reg::TransCount, Reg::Ctrl},
        {Reg::Ctrl, Reg::ReadAddr, Reg::WriteAddr, Reg::TransCount},
        {Reg::Ctrl, Reg::TransCount, Reg::ReadAddr, Reg::WriteAddr},
        {Reg::Ctrl, Reg::WriteAddr, Reg::TransCount, Reg::ReadAddr},
    };
    return alias[(offset % 0x40) / 0x10][(offset % 0x10) / 4];
}

uint32_t
RP2350DMA::readRegister(Addr offset)
{
    if (offset < CHANNELS_END) {
        return readChannel(offset / 0x40, aliasRegister(offset));
    }
    if (offset >= INTE0 && offset < IRQS_END && (offset & 0xF) != 0) {
        const unsigned n = (offset - INTR) / 0x10;
        switch (offset & 0xF) {
          case INTE0 & 0xF: return inte[n];
          case INTF0 & 0xF: return intf[n];
          default: return (intr & inte[n]) | intf[n];
        }
    }
    switch (offset) {
      case INTR:
        return intr;
      case MULTI_CHAN_TRIGGER:
        return 0;
      case CHAN_ABORT: {
        // A bit stays set until the channel's last transfer is written
        uint32_t bits = 0;
        for (unsigned i = 0; i < NumChannels; ++i)
            bits |= channels[i].aborting ? 1u << i : 0;
        return bits;
      }
      case N_CHANNELS:
        return NumChannels;
      default:
        unmodelled(offset, false);
        return 0;
    }
}

void
RP2350DMA::writeRegister(Addr offset, uint32_t value, Op op)
{
    // An atomic alias combines the written value with the register's.
    auto combine = [op, value](uint32_t current) -> uint32_t {
        switch (op) {
          case Op::Put: return value;
          case Op::Xor: return current ^ value;
          case Op::Set: return current | value;
          case Op::Clr: return current & ~value;
        }
        return value;
    };
    // The bits a write names in a register that acts on written ones
    const uint32_t ones = (op == Op::Clr ? 0 : value) & 0xFFFF;

    if (offset < CHANNELS_END) {
        const unsigned index = offset / 0x40;
        const unsigned column = (offset % 0x10) / 4;
        const Reg reg = aliasRegister(offset);
        Channel &c = channels[index];
        uint32_t current;
        switch (reg) {
          case Reg::TransCount: current = c.countReload; break;
          case Reg::Ctrl: current = c.ctrl; break;
          default: current = readChannel(index, reg); break;
        }
        const uint32_t written = combine(current);
        writeChannel(index, reg, written);
        // The last register of each row triggers the channel, unless 0
        // is written: a null trigger, which interrupts a quiet channel.
        if (column == 3) {
            if (written != 0)
                trigger(index);
            else if (c.ctrl & CTRL_IRQ_QUIET)
                raise(1u << index);
        }
        return;
    }
    if (offset >= INTE0 && offset < IRQS_END && (offset & 0xF) != 0) {
        const unsigned n = (offset - INTR) / 0x10;
        switch (offset & 0xF) {
          case INTE0 & 0xF:
            inte[n] = combine(inte[n]) & 0xFFFF;
            updateIrqs();
            break;
          case INTF0 & 0xF:
            intf[n] = combine(intf[n]) & 0xFFFF;
            updateIrqs();
            break;
          default:
            // INTS: writing a channel's bit clears its interrupt
            intr &= ~ones;
            updateIrqs();
            break;
        }
        return;
    }
    switch (offset) {
      case INTR:
        intr &= ~ones;
        updateIrqs();
        break;
      case MULTI_CHAN_TRIGGER:
        for (unsigned i = 0; i < NumChannels; ++i) {
            if (ones & (1u << i))
                trigger(i);
        }
        break;
      case CHAN_ABORT:
        for (unsigned i = 0; i < NumChannels; ++i) {
            if (ones & (1u << i))
                abort(i);
        }
        break;
      case N_CHANNELS:
        break;
      default:
        unmodelled(offset, true);
        break;
    }
}

Tick
RP2350DMA::read(PacketPtr pkt)
{
    const Addr byte = pkt->getAddr() - pioAddr;
    fatal_if(pkt->getSize() != 4 || (byte & 3),
             "RP2350DMA: %d-byte read at %#x; registers take 32-bit accesses",
             pkt->getSize(), byte);
    pkt->setLE<uint32_t>(readRegister(byte % BLOCK_SIZE));
    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
RP2350DMA::write(PacketPtr pkt)
{
    const Addr byte = pkt->getAddr() - pioAddr;
    fatal_if(pkt->getSize() != 4 || (byte & 3),
             "RP2350DMA: %d-byte write at %#x; registers take 32-bit accesses",
             pkt->getSize(), byte);
    writeRegister(byte % BLOCK_SIZE, pkt->getLE<uint32_t>(),
                  static_cast<Op>(byte / BLOCK_SIZE));
    pkt->makeAtomicResponse();
    return pioDelay;
}

// ---- interrupts -------------------------------------------------------

void
RP2350DMA::raise(uint32_t channel_bits)
{
    intr |= channel_bits;
    updateIrqs();
}

// Drives each output to its level.
void
RP2350DMA::updateIrqs()
{
    for (unsigned n = 0; n < irqPins.size(); ++n) {
        if ((intr & inte[n]) | intf[n])
            irqPins[n]->raise();
        else
            irqPins[n]->lower();
    }
}

// ---- channels ---------------------------------------------------------

void
RP2350DMA::dreq(unsigned line, unsigned count)
{
    for (Channel &c : channels) {
        if (c.treq() == line)
            c.credit = std::min<unsigned>(MaxCredit, c.credit + count);
    }
    wake();
}

void
RP2350DMA::trigger(unsigned index)
{
    Channel &c = channels[index];
    // A disabled or running channel ignores triggers.
    if (!(c.ctrl & CTRL_EN) || c.busy)
        return;
    c.count = c.countReload & CountMask;
    c.busy = true;
    DPRINTF(RP2350DMA, "ch%d triggered: %d transfers, read %#x write %#x\n",
            index, c.count, c.readAddr, c.writeAddr);
    if (c.count == 0)
        finish(index);
    else
        wake();
}

void
RP2350DMA::abort(unsigned index)
{
    Channel &c = channels[index];
    c.count = 0;
    if (!c.busy)
        return;
    // Transfers already started still complete; BUSY and the channel's
    // CHAN_ABORT bit stay set until they have.
    c.aborting = true;
    DPRINTF(RP2350DMA, "ch%d aborted with %d transfers in flight\n", index,
            c.inFlight);
    if (c.inFlight == 0)
        finish(index);
}

// The end of a transfer sequence: every transfer written, or flushed
// after an abort.
void
RP2350DMA::finish(unsigned index)
{
    Channel &c = channels[index];
    const bool aborted = c.aborting;
    c.busy = false;
    c.aborting = false;
    DPRINTF(RP2350DMA, "ch%d %s\n", index, aborted ? "stopped" : "complete");
    if (aborted)
        return;
    if (!(c.ctrl & CTRL_IRQ_QUIET))
        raise(1u << index);
    // A chain trigger acts at the next clock edge.
    if (c.chainTo() != index) {
        pendingTriggers |= 1u << c.chainTo();
        wake();
    }
    // A self-triggering channel carries straight on, still busy.
    if (c.mode() == ModeTriggerSelf && (c.countReload & CountMask) != 0)
        trigger(index);
}

bool
RP2350DMA::requesting(const Channel &c) const
{
    if (!c.busy || c.aborting || !(c.ctrl & CTRL_EN) || c.count == 0)
        return false;
    const unsigned treq = c.treq();
    return treq == TreqPermanent || (treq < TreqTimer0 && c.credit > 0);
}

// Takes the channel's next pair of addresses and moves its registers on.
RP2350DMA::Transfer *
RP2350DMA::start(unsigned index)
{
    Channel &c = channels[index];
    const unsigned size_field = (c.ctrl >> CTRL_DATA_SIZE_LSB) & 0x3;
    const unsigned size = size_field == 0 ? 1 : size_field == 1 ? 2 : 4;
    const unsigned ring_bits = (c.ctrl >> CTRL_RING_SIZE_LSB) & 0xF;
    const bool ring_write = c.ctrl & CTRL_RING_SEL;

    auto *t = new Transfer;
    t->channel = index;
    t->readAddr = c.readAddr;
    t->writeAddr = c.writeAddr;
    t->size = size;

    // An address with a ring wraps within its low ring_bits bits.
    auto next = [size, ring_bits](uint32_t addr, bool ring) -> uint32_t {
        if (!ring || ring_bits == 0)
            return addr + size;
        const uint32_t mask = (1u << ring_bits) - 1;
        return (addr & ~mask) | ((addr + size) & mask);
    };
    if (c.ctrl & CTRL_INCR_READ)
        c.readAddr = next(c.readAddr, !ring_write);
    if (c.ctrl & CTRL_INCR_WRITE)
        c.writeAddr = next(c.writeAddr, ring_write);
    if (c.mode() != ModeEndless)
        --c.count;
    if (c.treq() != TreqPermanent)
        --c.credit;
    ++c.inFlight;
    ++stats.transfers[index];
    return t;
}

void
RP2350DMA::recvResponse(PacketPtr pkt, bool is_read)
{
    fatal_if(pkt->isError(), "RP2350DMA: bus error on a %s of %#x; bus "
             "errors are not modelled", is_read ? "read" : "write",
             pkt->getAddr());
    auto *t = dynamic_cast<Transfer *>(pkt->popSenderState());
    panic_if(!t, "RP2350DMA: response without its transfer");
    delete pkt;

    if (is_read) {
        t->haveData = true;
        t->dataTick = curTick();
        wake();
    } else {
        const unsigned index = t->channel;
        delete t;
        --writesInFlight;
        Channel &c = channels[index];
        --c.inFlight;
        if (c.busy && c.inFlight == 0 && (c.aborting || c.count == 0))
            finish(index);
    }
    if (drainState() == DrainState::Draining && idle())
        signalDrainDone();
}

// One clock cycle: at most one write and one read leave.
void
RP2350DMA::tick()
{
    if (pendingTriggers) {
        const uint32_t bits = pendingTriggers;
        pendingTriggers = 0;
        for (unsigned i = 0; i < NumChannels; ++i) {
            if (bits & (1u << i))
                trigger(i);
        }
    }

    // Write manager: the oldest transfer, the cycle after its data came.
    if (!writePort.waiting() && !dataFifo.empty()) {
        Transfer *t = dataFifo.front();
        if (t->haveData && t->dataTick < curTick()) {
            dataFifo.pop_front();
            ++writesInFlight;
            auto req = std::make_shared<Request>(t->writeAddr, t->size, 0,
                                                 requestorId);
            PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
            pkt->dataStatic(t->data);
            pkt->pushSenderState(t);
            writePort.send(pkt);
        }
    }

    // Read manager: the next channel, round robin, with a transfer to make.
    if (!readPort.waiting() && dataFifo.size() < DataFifoDepth &&
        drainState() != DrainState::Draining) {
        for (unsigned n = 0; n < NumChannels; ++n) {
            const unsigned index = (nextChannel + n) % NumChannels;
            if (!requesting(channels[index]))
                continue;
            nextChannel = (index + 1) % NumChannels;
            Transfer *t = start(index);
            dataFifo.push_back(t);
            auto req = std::make_shared<Request>(t->readAddr, t->size, 0,
                                                 requestorId);
            PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
            pkt->dataStatic(t->data);
            pkt->pushSenderState(t);
            readPort.send(pkt);
            break;
        }
    }

    bool work = pendingTriggers || !dataFifo.empty();
    if (drainState() != DrainState::Draining) {
        for (const Channel &c : channels)
            work |= requesting(c);
    }
    if (work)
        wake();
}

void
RP2350DMA::wake()
{
    if (!tickEvent.scheduled())
        schedule(tickEvent, clockEdge(Cycles(1)));
}

bool
RP2350DMA::idle() const
{
    return dataFifo.empty() && writesInFlight == 0 && !readPort.waiting() &&
           !writePort.waiting();
}

DrainState
RP2350DMA::drain()
{
    return idle() ? DrainState::Drained : DrainState::Draining;
}

void
RP2350DMA::drainResume()
{
    wake();
}

void
RP2350DMA::serialize(CheckpointOut &cp) const
{
    BasicPioDevice::serialize(cp);
    for (unsigned i = 0; i < NumChannels; ++i) {
        const Channel &c = channels[i];
        paramOut(cp, csprintf("ch%d.readAddr", i), c.readAddr);
        paramOut(cp, csprintf("ch%d.writeAddr", i), c.writeAddr);
        paramOut(cp, csprintf("ch%d.countReload", i), c.countReload);
        paramOut(cp, csprintf("ch%d.count", i), c.count);
        paramOut(cp, csprintf("ch%d.ctrl", i), c.ctrl);
        paramOut(cp, csprintf("ch%d.busy", i), c.busy);
        paramOut(cp, csprintf("ch%d.aborting", i), c.aborting);
        paramOut(cp, csprintf("ch%d.credit", i), c.credit);
    }
    SERIALIZE_SCALAR(intr);
    SERIALIZE_ARRAY(inte, NumIrqs);
    SERIALIZE_ARRAY(intf, NumIrqs);
    SERIALIZE_SCALAR(pendingTriggers);
    SERIALIZE_SCALAR(nextChannel);
}

void
RP2350DMA::unserialize(CheckpointIn &cp)
{
    BasicPioDevice::unserialize(cp);
    for (unsigned i = 0; i < NumChannels; ++i) {
        Channel &c = channels[i];
        paramIn(cp, csprintf("ch%d.readAddr", i), c.readAddr);
        paramIn(cp, csprintf("ch%d.writeAddr", i), c.writeAddr);
        paramIn(cp, csprintf("ch%d.countReload", i), c.countReload);
        paramIn(cp, csprintf("ch%d.count", i), c.count);
        paramIn(cp, csprintf("ch%d.ctrl", i), c.ctrl);
        paramIn(cp, csprintf("ch%d.busy", i), c.busy);
        paramIn(cp, csprintf("ch%d.aborting", i), c.aborting);
        paramIn(cp, csprintf("ch%d.credit", i), c.credit);
    }
    UNSERIALIZE_SCALAR(intr);
    UNSERIALIZE_ARRAY(inte, NumIrqs);
    UNSERIALIZE_ARRAY(intf, NumIrqs);
    UNSERIALIZE_SCALAR(pendingTriggers);
    UNSERIALIZE_SCALAR(nextChannel);
}

} // namespace gem5
