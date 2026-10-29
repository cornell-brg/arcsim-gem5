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

unsigned
RP2350DMA::Channel::size() const
{
    const unsigned field = (ctrl >> CTRL_DATA_SIZE_LSB) & 0x3;
    return field == 0 ? 1 : field == 1 ? 2 : 4;
}

bool
RP2350DMA::Channel::linearRead() const
{
    const unsigned ring_bits = (ctrl >> CTRL_RING_SIZE_LSB) & 0xF;
    return (ctrl & CTRL_INCR_READ) && (ring_bits == 0 || (ctrl & CTRL_RING_SEL));
}

bool
RP2350DMA::Channel::linearWrite() const
{
    const unsigned ring_bits = (ctrl >> CTRL_RING_SIZE_LSB) & 0xF;
    return (ctrl & CTRL_INCR_WRITE) &&
           (ring_bits == 0 || !(ctrl & CTRL_RING_SEL));
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
      rafDepth(p.raf_depth), wafDepth(p.waf_depth), tdfDepth(p.tdf_depth),
      tickEvent([this] { tick(); }, name() + ".tick", false,
                Event::CPU_Tick_Pri),
      stats(*this)
{
    fatal_if(p.port_irq_connection_count > NumIrqs,
             "RP2350DMA has %d interrupt outputs", NumIrqs);
    fatal_if(rafDepth == 0 || wafDepth == 0 || tdfDepth == 0,
             "RP2350DMA: the FIFO depths must be at least 1");
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

Cycles
RP2350DMA::nowCycle() const
{
    // curCycle() rounds up to the next clock edge; a tick between edges falls
    // in the cycle that began at the edge before it.
    return clockEdge() == curTick() ? curCycle() : Cycles(curCycle() - 1);
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
    // While an aborted channel drains, an address that increments without a
    // ring reads as the transfers written so far, as on a Pico 2.
    const uint32_t back = c.aborting ? c.inFlight * c.size() : 0;
    switch (reg) {
      case Reg::ReadAddr:
        return c.readAddr - (c.linearRead() ? back : 0);
      case Reg::WriteAddr:
        return c.writeAddr - (c.linearWrite() ? back : 0);
      // The transfers left, under the mode last written
      case Reg::TransCount: return (c.countReload & ~CountMask) | c.count;
      case Reg::Ctrl:
        return c.ctrl | (c.busy ? CTRL_BUSY : 0) |
               (c.readError ? CTRL_READ_ERROR : 0) |
               (c.writeError ? CTRL_WRITE_ERROR : 0) |
               (c.failed() ? CTRL_AHB_ERROR : 0);
    }
    panic("RP2350DMA: bad channel register");
}

void
RP2350DMA::writeChannel(unsigned index, Reg reg, uint32_t value, Op op,
                        uint32_t raw)
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
        // READ_ERROR and WRITE_ERROR: write one to clear
        if (op != Op::Clr && (raw & CTRL_READ_ERROR))
            c.readError = false;
        if (op != Op::Clr && (raw & CTRL_WRITE_ERROR))
            c.writeError = false;
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
    if (offset >= DBG_START && offset < DBG_END) {
        const Channel &c = channels[(offset - DBG_START) / 0x40];
        switch (offset % 0x40) {
          case DBG_CTDREQ: return c.credit;
          case DBG_TCR: return c.countReload;
          default:
            unmodelled(offset, false);
            return 0;
        }
    }
    switch (offset) {
      case INTR:
        return intr;
      case MULTI_CHAN_TRIGGER:
        return 0;
      case FIFO_LEVELS: {
        // RAF_LVL 23:16, WAF_LVL 15:8, TDF_LVL 7:0
        const Cycles now = nowCycle();
        uint32_t tdf_level = 0;
        for (const Transfer *t : tdf)
            tdf_level += t->ready <= now;
        return std::min<uint32_t>(raf.size(), 0xFF) << 16 |
               std::min<uint32_t>(waf.size(), 0xFF) << 8 |
               std::min<uint32_t>(tdf_level, 0xFF);
      }
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
          case Reg::ReadAddr: current = c.readAddr; break;
          case Reg::WriteAddr: current = c.writeAddr; break;
          case Reg::TransCount: current = c.countReload; break;
          default: current = c.ctrl; break;
        }
        const uint32_t written = combine(current);
        writeChannel(index, reg, written, op, value);
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
    if (offset >= DBG_START && offset < DBG_END) {
        // DBG_CTDREQ: any write clears the counter and starts the DREQ
        // handshake again; DBG_TCR is read-only.
        if (offset % 0x40 == DBG_CTDREQ)
            restartDreq((offset - DBG_START) / 0x40);
        else if (offset % 0x40 != DBG_TCR)
            unmodelled(offset, true);
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
      case FIFO_LEVELS:
      case N_CHANNELS:
        break;
      default:
        unmodelled(offset, true);
        break;
    }
}

// Registers ignore an access's width (datasheet 2.1.5): a narrow read returns
// the register's bytes on its lanes, and a narrow write is a 32-bit write of
// its data replicated across the word, as the cores and the DMA put it on the
// bus.
Tick
RP2350DMA::read(PacketPtr pkt)
{
    const Addr byte = pkt->getAddr() - pioAddr;
    const unsigned size = pkt->getSize();
    fatal_if((size != 1 && size != 2 && size != 4) || (byte & (size - 1)),
             "RP2350DMA: %d-byte read at %#x", size, byte);
    const uint32_t value = readRegister((byte % BLOCK_SIZE) & ~Addr(3));
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = value >> (8 * i);
    pkt->setData(bytes + (byte & 3));
    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
RP2350DMA::write(PacketPtr pkt)
{
    const Addr byte = pkt->getAddr() - pioAddr;
    const unsigned size = pkt->getSize();
    fatal_if((size != 1 && size != 2 && size != 4) || (byte & (size - 1)),
             "RP2350DMA: %d-byte write at %#x", size, byte);
    uint8_t bytes[4] = {};
    pkt->writeData(bytes);
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= uint32_t(bytes[i % size]) << (8 * i);
    writeRegister((byte % BLOCK_SIZE) & ~Addr(3), value,
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
RP2350DMA::addDreqSource(unsigned line, DreqSource *source)
{
    dreqSources.emplace_back(line, source);
}

// Clears a channel's DREQ counter and tells the peripherals on its line,
// which send again what they hold.
void
RP2350DMA::restartDreq(unsigned index)
{
    Channel &c = channels[index];
    c.credit = 0;
    const unsigned line = c.treq();
    if (line >= TreqTimer0)
        return;
    for (const auto &[source_line, source] : dreqSources) {
        if (source_line == line)
            source->dreqRestart(line);
    }
}

void
RP2350DMA::trigger(unsigned index)
{
    Channel &c = channels[index];
    // A disabled, running or failed channel ignores triggers.
    if (!(c.ctrl & CTRL_EN) || c.busy || c.failed())
        return;
    restartDreq(index);
    c.count = c.countReload & CountMask;
    c.busy = true;
    // A count of 0 runs one null transfer, which takes the pipeline's time
    // and no bus access, as on a Pico 2.
    c.nullPending = c.count == 0;
    DPRINTF(RP2350DMA, "ch%d triggered: %d transfers, read %#x write %#x\n",
            index, c.count, c.readAddr, c.writeAddr);
    wake();
}

void
RP2350DMA::abort(unsigned index)
{
    Channel &c = channels[index];
    c.count = 0;
    c.nullPending = false;
    if (!c.busy)
        return;
    // Transfers already generated still complete; BUSY and the channel's
    // CHAN_ABORT bit stay set until they have.
    c.aborting = true;
    DPRINTF(RP2350DMA, "ch%d aborted with %d transfers in flight\n", index,
            c.inFlight);
    checkFinish(index);
}

// A bus error: the channel stops, its later transfers make no bus access,
// and it interrupts whether quiet or not (datasheet 12.6.7).
void
RP2350DMA::fail(unsigned index, bool on_read)
{
    Channel &c = channels[index];
    if (on_read)
        c.readError = true;
    else
        c.writeError = true;
    c.nullPending = false;
    raise(1u << index);
    DPRINTF(RP2350DMA, "ch%d bus error on a %s\n", index,
            on_read ? "read" : "write");
}

void
RP2350DMA::checkFinish(unsigned index)
{
    const Channel &c = channels[index];
    if (c.busy && c.inFlight == 0 && !c.nullPending &&
        (c.aborting || c.failed() || c.count == 0))
        finish(index);
}

// The end of a transfer sequence: every transfer written, or the rest given
// up after an abort or a bus error.
void
RP2350DMA::finish(unsigned index)
{
    Channel &c = channels[index];
    const bool completed = !c.aborting && !c.failed();
    c.busy = false;
    c.aborting = false;
    DPRINTF(RP2350DMA, "ch%d %s\n", index, completed ? "complete" : "stopped");
    if (!completed)
        return;
    if (!(c.ctrl & CTRL_IRQ_QUIET))
        raise(1u << index);
    // A chain trigger acts at the next clock edge.
    if (c.chainTo() != index) {
        pendingTriggers |= 1u << c.chainTo();
        wake();
    }
    // A self-triggering channel carries straight on, still busy.
    if (c.mode() == ModeTriggerSelf && (c.countReload & CountMask) != 0 &&
        (c.ctrl & CTRL_EN)) {
        c.count = c.countReload & CountMask;
        c.busy = true;
    }
}

bool
RP2350DMA::requesting(const Channel &c) const
{
    if (!c.busy || c.aborting || c.failed() || !(c.ctrl & CTRL_EN))
        return false;
    // a null transfer needs no DREQ: it moves no data
    if (c.nullPending)
        return true;
    if (c.count == 0)
        return false;
    const unsigned treq = c.treq();
    return treq == TreqPermanent || (treq < TreqTimer0 && c.credit > 0);
}

// The address generator: the channel's next pair of addresses into the RAF
// and the WAF, and its registers moved on.
void
RP2350DMA::generate(unsigned index, Cycles now)
{
    Channel &c = channels[index];
    auto *t = new Transfer;
    t->channel = index;
    t->readAddr = c.readAddr;
    t->writeAddr = c.writeAddr;
    t->size = c.size();
    t->generated = now;
    ++c.inFlight;
    raf.push_back(t);
    waf.push_back(t);
    if (c.nullPending) {
        t->null = true;
        c.nullPending = false;
        return;
    }

    const unsigned ring_bits = (c.ctrl >> CTRL_RING_SIZE_LSB) & 0xF;
    const bool ring_write = c.ctrl & CTRL_RING_SEL;
    const unsigned size = t->size;
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
    if (c.treq() != TreqPermanent && c.credit > 0)
        --c.credit;
    ++stats.transfers[index];
}

void
RP2350DMA::recvResponse(PacketPtr pkt, bool is_read)
{
    auto *t = dynamic_cast<Transfer *>(pkt->popSenderState());
    panic_if(!t, "RP2350DMA: response without its transfer");
    const bool error = pkt->isError();
    delete pkt;
    // The answer ends the access's data phase, which on an AHB-Lite bus is
    // the cycle after the one it is answered in.
    const Cycles answered = nowCycle();

    if (is_read) {
        panic_if(t != reading, "RP2350DMA: a read answered out of order");
        reading = nullptr;
        readFree = Cycles(answered + 1);
        t->ready = Cycles(answered + 2);
        lastTdfPush = Cycles(answered + 1);
        tdfPushed = true;
        if (error) {
            t->dead = true;
            fail(t->channel, true);
        }
        tdf.push_back(t);
    } else {
        panic_if(t != writing, "RP2350DMA: a write answered out of order");
        writing = nullptr;
        writeFree = Cycles(answered + 1);
        // A faulting write counts as written; the later ones are dropped.
        if (error)
            fail(t->channel, false);
        retiring.push_back({Cycles(answered + 2), t->channel, false, false});
        delete t;
    }
    wake();
    if (drainState() == DrainState::Draining && idle())
        signalDrainDone();
}

// Transfers leaving the pipeline by now: written, or dropped, when a dropped
// one gives its step back so that the registers end up showing the transfers
// that completed (an address only when it increments without a ring).
void
RP2350DMA::retire(Cycles now)
{
    while (!retiring.empty() && retiring.front().when <= now) {
        const Retire r = retiring.front();
        retiring.pop_front();
        Channel &c = channels[r.channel];
        panic_if(c.inFlight == 0, "RP2350DMA: ch%d retired more than it "
                 "made", r.channel);
        --c.inFlight;
        if (r.dropped && !r.null) {
            if (c.linearRead())
                c.readAddr -= c.size();
            if (c.linearWrite())
                c.writeAddr -= c.size();
            if (c.mode() != ModeEndless)
                ++c.count;
        }
        checkFinish(r.channel);
    }
}

// Read manager: the RAF's oldest transfer, from the cycle after it was made,
// while the TDF has room for its data. A null transfer, or one whose channel
// has stopped on a bus error, goes to the TDF as failed without a bus access,
// once no read's data is still on its way (it goes in first).
void
RP2350DMA::readSide(Cycles now)
{
    if (raf.empty() || readPort.waiting() || reading)
        return;
    Transfer *t = raf.front();
    if (t->generated >= now || tdf.size() >= tdfDepth)
        return;
    if (t->null || channels[t->channel].failed()) {
        if (tdfPushed && lastTdfPush >= now)
            return;
        raf.pop_front();
        t->dead = true;
        t->ready = Cycles(now + 1);
        tdf.push_back(t);
        return;
    }
    if (now < readFree)
        return;
    raf.pop_front();
    reading = t;
    auto req = std::make_shared<Request>(t->readAddr, t->size, 0,
                                         requestorId);
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
    pkt->dataStatic(t->data);
    pkt->pushSenderState(t);
    readPort.send(pkt);
}

// Write manager: the WAF's oldest transfer with its data from the TDF. One
// whose read failed or was skipped, or whose channel has stopped on a bus
// error, is dropped without a write.
void
RP2350DMA::writeSide(Cycles now)
{
    if (waf.empty() || tdf.empty() || writePort.waiting())
        return;
    Transfer *t = tdf.front();
    panic_if(t != waf.front(), "RP2350DMA: the TDF lost the WAF's order");
    if (t->ready > now)
        return;
    if (t->dead || channels[t->channel].failed()) {
        waf.pop_front();
        tdf.pop_front();
        retiring.push_back({Cycles(now + 1), t->channel, true, t->null});
        delete t;
        return;
    }
    if (writing || now < writeFree)
        return;
    waf.pop_front();
    tdf.pop_front();
    writing = t;
    auto req = std::make_shared<Request>(t->writeAddr, t->size, 0,
                                         requestorId);
    PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
    pkt->dataStatic(t->data);
    pkt->pushSenderState(t);
    writePort.send(pkt);
}

// One clock cycle: transfers leaving, chain triggers, the write side, the
// read side and then the address generator, as the chip's pipeline goes.
void
RP2350DMA::tick()
{
    const Cycles now = nowCycle();
    retire(now);

    if (pendingTriggers) {
        const uint32_t bits = pendingTriggers;
        pendingTriggers = 0;
        for (unsigned i = 0; i < NumChannels; ++i) {
            if (bits & (1u << i))
                trigger(i);
        }
    }

    writeSide(now);
    readSide(now);

    // Address generator: the next channel, round robin, with a transfer to
    // make, while the RAF and the WAF have room.
    if (raf.size() < rafDepth && waf.size() < wafDepth &&
        drainState() != DrainState::Draining) {
        for (unsigned n = 0; n < NumChannels; ++n) {
            const unsigned index = (nextChannel + n) % NumChannels;
            if (!requesting(channels[index]))
                continue;
            nextChannel = (index + 1) % NumChannels;
            generate(index, now);
            break;
        }
    }

    bool work = pendingTriggers || !raf.empty() || !waf.empty() ||
                !retiring.empty();
    for (const Channel &c : channels)
        work |= requesting(c);
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
    return raf.empty() && waf.empty() && tdf.empty() && !reading &&
           !writing && retiring.empty() && !readPort.waiting() &&
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
        paramOut(cp, csprintf("ch%d.readError", i), c.readError);
        paramOut(cp, csprintf("ch%d.writeError", i), c.writeError);
        paramOut(cp, csprintf("ch%d.nullPending", i), c.nullPending);
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
        paramIn(cp, csprintf("ch%d.readError", i), c.readError);
        paramIn(cp, csprintf("ch%d.writeError", i), c.writeError);
        paramIn(cp, csprintf("ch%d.nullPending", i), c.nullPending);
        paramIn(cp, csprintf("ch%d.credit", i), c.credit);
    }
    UNSERIALIZE_SCALAR(intr);
    UNSERIALIZE_ARRAY(inte, NumIrqs);
    UNSERIALIZE_ARRAY(intf, NumIrqs);
    UNSERIALIZE_SCALAR(pendingTriggers);
    UNSERIALIZE_SCALAR(nextChannel);
}

} // namespace gem5
