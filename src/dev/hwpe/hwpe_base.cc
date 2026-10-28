#include "dev/hwpe/hwpe_base.hh"

#include <algorithm>

#include "base/cast.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "mem/packet_access.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{

// ---- TCDM port --------------------------------------------------------

HWPEBase::TcdmPort::TcdmPort(const std::string &name, HWPEBase &_hwpe)
    : RequestPort(name), hwpe(_hwpe)
{
}

void
HWPEBase::TcdmPort::send(PacketPtr pkt)
{
    if (!sendTimingReq(pkt))
        held = pkt;
}

PacketPtr
HWPEBase::TcdmPort::drop()
{
    PacketPtr pkt = held;
    held = nullptr;
    return pkt;
}

bool
HWPEBase::TcdmPort::recvTimingResp(PacketPtr pkt)
{
    hwpe.recvTcdmRespFromPort(pkt);
    return true;
}

void
HWPEBase::TcdmPort::recvReqRetry()
{
    // Nothing to resend if a soft clear has dropped the refused packet
    if (!held)
        return;
    PacketPtr pkt = held;
    held = nullptr;
    send(pkt);
    if (!held)
        hwpe.tcdmRetriedFromPort();
}

// ---- construction -----------------------------------------------------

HWPEBase::HWPEBase(const Params &p)
    : BasicPioDevice(p, p.pio_size),
      requestorId(p.system->getRequestorId(this)),
      streamer(*this),
      tcdmPort(name() + ".tcdm_port", *this),
      numContexts(p.num_contexts),
      startLatency(p.start_latency),
      doneLatency(p.done_latency),
      generic(p.num_generic_regs, 0),
      io(p.num_contexts, std::vector<uint32_t>(p.num_io_regs, 0)),
      startEvent([this] { start(); }, name() + ".start"),
      doneEvent([this] { finish(); }, name() + ".done"),
      irqLowerEvent([this] { irqPin->lower(); }, name() + ".irqLower")
{
    fatal_if(numContexts < 1 || numContexts > MaxContexts,
             "%s: %d contexts; 1 to %d", name(), numContexts, MaxContexts);
    fatal_if(p.num_generic_regs > MaxGenericRegs,
             "%s: %d generic registers; at most %d", name(),
             p.num_generic_regs, MaxGenericRegs);
    fatal_if(p.num_io_regs > MaxIoRegs, "%s: %d job registers; at most %d",
             name(), p.num_io_regs, MaxIoRegs);
    fatal_if(p.pio_size < WINDOW_END,
             "%s: a %#x-byte window; the registers take %#x", name(),
             p.pio_size, WINDOW_END);
    if (p.port_irq_connection_count) {
        irqPin = std::make_unique<IntSourcePin<HWPEBase>>(
            name() + ".irq", 0, this);
    }
}

Port &
HWPEBase::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "tcdm_port")
        return tcdmPort;
    if (if_name == "irq") {
        fatal_if(!irqPin, "%s: irq is not connected", name());
        return *irqPin;
    }
    return BasicPioDevice::getPort(if_name, idx);
}

void
HWPEBase::init()
{
    BasicPioDevice::init();
    fatal_if(!tcdmPort.isConnected(), "%s: connect tcdm_port", name());
}

// ---- registers --------------------------------------------------------

void
HWPEBase::unmodelled(Addr offset, bool is_write)
{
    if (warned.insert(offset).second)
        warn("%s: no register at %#x (%s); it reads 0 and ignores writes\n",
             name(), offset, is_write ? "write" : "read");
}

// STATUS: bit 8*n is set while context n has a job queued or running.
uint32_t
HWPEBase::status() const
{
    uint32_t value = 0;
    for (unsigned n = 0; n < pending; ++n)
        value |= 1u << (8 * ((runningContext + n) % numContexts));
    return value;
}

uint32_t
HWPEBase::readRegister(Addr offset)
{
    switch (offset) {
      case TRIGGER:
      case SOFT_CLEAR:
      case SW_EVT:
        return 0;
      case RESERVED:
        return extFlags();
      case ACQUIRE:
        if (acquired)
            return AcquireLocked;
        if (pending == numContexts)
            return AcquireFull;
        acquired = true;
        DPRINTF(HWPE, "Acquired: job %d in context %d\n", jobId,
                pointerContext);
        return jobId++;
      case FINISHED: {
        const uint32_t value = finished;
        finished = 0;
        return value;
      }
      case STATUS:
        return status();
      case RUNNING_JOB:
        return doneJobs;
      default:
        break;
    }
    if (offset >= GENERIC && offset < IO &&
        (offset - GENERIC) / 4 < generic.size()) {
        return generic[(offset - GENERIC) / 4];
    }
    if (offset >= IO && offset < WINDOW_END &&
        (offset - IO) / 4 < io[pointerContext].size()) {
        return io[pointerContext][(offset - IO) / 4];
    }
    unmodelled(offset, false);
    return 0;
}

// mask selects the bytes the access writes; the other bytes keep their
// value.
void
HWPEBase::writeRegister(Addr offset, uint32_t value, uint32_t mask)
{
    switch (offset) {
      case TRIGGER:
        trigger();
        return;
      case SOFT_CLEAR:
        softClear((value & mask) == 0);
        return;
      // Read-only registers, and registers whose writes have no effect
      case ACQUIRE:
      case FINISHED:
      case STATUS:
      case RUNNING_JOB:
      case RESERVED:
      case SW_EVT:
        return;
      default:
        break;
    }
    uint32_t *reg = nullptr;
    if (offset >= GENERIC && offset < IO &&
        (offset - GENERIC) / 4 < generic.size()) {
        reg = &generic[(offset - GENERIC) / 4];
    } else if (offset >= IO && offset < WINDOW_END &&
               (offset - IO) / 4 < io[pointerContext].size()) {
        reg = &io[pointerContext][(offset - IO) / 4];
    }
    if (!reg) {
        unmodelled(offset, true);
        return;
    }
    *reg = (*reg & ~mask) | (value & mask);
}

Tick
HWPEBase::read(PacketPtr pkt)
{
    const Addr byte = pkt->getAddr() - pioAddr;
    const unsigned size = pkt->getSize();
    fatal_if((size != 1 && size != 2 && size != 4) || (byte & (size - 1)),
             "%s: %d-byte read at %#x", name(), size, byte);
    const uint32_t value = readRegister(byte & ~Addr(3)) >> (8 * (byte & 3));
    switch (size) {
      case 1: pkt->setLE<uint8_t>(value); break;
      case 2: pkt->setLE<uint16_t>(value); break;
      default: pkt->setLE<uint32_t>(value); break;
    }
    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
HWPEBase::write(PacketPtr pkt)
{
    const Addr byte = pkt->getAddr() - pioAddr;
    const unsigned size = pkt->getSize();
    fatal_if((size != 1 && size != 2 && size != 4) || (byte & (size - 1)),
             "%s: %d-byte write at %#x", name(), size, byte);
    const unsigned shift = 8 * (byte & 3);
    uint32_t value;
    uint32_t mask;
    switch (size) {
      case 1:
        value = pkt->getLE<uint8_t>();
        mask = 0xFF;
        break;
      case 2:
        value = pkt->getLE<uint16_t>();
        mask = 0xFFFF;
        break;
      default:
        value = pkt->getLE<uint32_t>();
        mask = 0xFFFFFFFF;
        break;
    }
    writeRegister(byte & ~Addr(3), value << shift, mask << shift);
    pkt->makeAtomicResponse();
    return pioDelay;
}

// ---- jobs -------------------------------------------------------------

void
HWPEBase::trigger()
{
    if (pending == numContexts) {
        warn("%s: TRIGGER while every context holds a job; ignored\n",
             name());
        return;
    }
    DPRINTF(HWPE, "Triggered: context %d\n", pointerContext);
    acquired = false;
    pointerContext = (pointerContext + 1) % numContexts;
    ++pending;
    if (!running && !startEvent.scheduled())
        schedule(startEvent, clockEdge(startLatency));
}

void
HWPEBase::start()
{
    DPRINTF(HWPE, "Job started: context %d\n", runningContext);
    running = true;
    startJob(io[runningContext]);
}

void
HWPEBase::jobDone()
{
    panic_if(!running || doneEvent.scheduled(),
             "%s: jobDone() without a running job", name());
    schedule(doneEvent, clockEdge(doneLatency));
}

void
HWPEBase::finish()
{
    DPRINTF(HWPE, "Job done: context %d\n", runningContext);
    running = false;
    runningContext = (runningContext + 1) % numContexts;
    --pending;
    ++doneJobs;
    ++finished;
    pulseIrq();
    if (pending)
        schedule(startEvent, clockEdge(startLatency));
}

void
HWPEBase::softClear(bool registers)
{
    DPRINTF(HWPE, "Soft clear%s\n", registers ? " with the registers" : "");
    if (startEvent.scheduled())
        deschedule(startEvent);
    if (doneEvent.scheduled())
        deschedule(doneEvent);
    clearJob();
    streamer.clear();
    // From now on, responses to accesses sent before this point go to
    // clearedTcdmResp() (see TcdmState)
    ++clears;
    // A packet the TCDM refused was never accepted, so no response will
    // come for it: hand it to clearedTcdmResp() now
    if (PacketPtr pkt = tcdmPort.drop()) {
        delete pkt->popSenderState();
        --outstanding;
        clearedTcdmResp(pkt);
        if (drainState() == DrainState::Draining && outstanding == 0)
            signalDrainDone();
    }
    pointerContext = 0;
    runningContext = 0;
    pending = 0;
    running = false;
    acquired = false;
    jobId = 0;
    doneJobs = 0;
    finished = 0;
    if (registers) {
        std::fill(generic.begin(), generic.end(), 0);
        for (auto &regs : io)
            std::fill(regs.begin(), regs.end(), 0);
    }
}

// ---- interrupt --------------------------------------------------------

void
HWPEBase::pulseIrq()
{
    if (!irqPin)
        return;
    irqPin->raise();
    reschedule(irqLowerEvent, clockEdge(Cycles(1)), true);
}

// ---- TCDM -------------------------------------------------------------

bool
HWPEBase::canSendTcdm() const
{
    return !tcdmPort.waiting() && drainState() != DrainState::Draining;
}

void
HWPEBase::sendTcdm(PacketPtr pkt)
{
    panic_if(!canSendTcdm(), "%s: TCDM access while the TCDM cannot take it",
             name());
    pkt->pushSenderState(new TcdmState(clears));
    ++outstanding;
    tcdmPort.send(pkt);
}

void
HWPEBase::recvTcdmResp(PacketPtr pkt)
{
    streamer.recvResp(pkt);
}

void
HWPEBase::tcdmRetried()
{
    streamer.retried();
}

void
HWPEBase::clearedTcdmResp(PacketPtr pkt)
{
    streamer.dropPacket(pkt);
}

void
HWPEBase::recvTcdmRespFromPort(PacketPtr pkt)
{
    auto *state = safe_cast<TcdmState *>(pkt->popSenderState());
    const bool cleared = state->clears != clears;
    delete state;
    --outstanding;
    if (cleared)
        clearedTcdmResp(pkt);
    else
        recvTcdmResp(pkt);
    if (drainState() == DrainState::Draining && outstanding == 0)
        signalDrainDone();
}

void
HWPEBase::tcdmRetriedFromPort()
{
    tcdmRetried();
}

// ---- checkpoints ------------------------------------------------------

DrainState
HWPEBase::drain()
{
    return outstanding == 0 ? DrainState::Drained : DrainState::Draining;
}

void
HWPEBase::drainResume()
{
    streamer.wake();
    resumeJob();
}

void
HWPEBase::serialize(CheckpointOut &cp) const
{
    panic_if(outstanding, "%s: checkpoint with TCDM accesses outstanding",
             name());
    SERIALIZE_CONTAINER(generic);
    for (unsigned n = 0; n < numContexts; ++n)
        arrayParamOut(cp, csprintf("io%d", n), io[n]);
    SERIALIZE_SCALAR(pointerContext);
    SERIALIZE_SCALAR(runningContext);
    SERIALIZE_SCALAR(pending);
    SERIALIZE_SCALAR(running);
    SERIALIZE_SCALAR(acquired);
    SERIALIZE_SCALAR(jobId);
    SERIALIZE_SCALAR(doneJobs);
    SERIALIZE_SCALAR(finished);
    // Each event as the tick it is due at; MaxTick if not scheduled
    const Tick start_tick =
        startEvent.scheduled() ? startEvent.when() : MaxTick;
    const Tick done_tick = doneEvent.scheduled() ? doneEvent.when() : MaxTick;
    const Tick irq_lower_tick =
        irqLowerEvent.scheduled() ? irqLowerEvent.when() : MaxTick;
    SERIALIZE_SCALAR(start_tick);
    SERIALIZE_SCALAR(done_tick);
    SERIALIZE_SCALAR(irq_lower_tick);
    // serializeJob() must come before the streamer's section: values
    // written after a subsection end up inside that subsection
    serializeJob(cp);
    streamer.serializeSection(cp, "streamer");
}

void
HWPEBase::unserialize(CheckpointIn &cp)
{
    const size_t num_generic = generic.size();
    const size_t num_io = io[0].size();
    UNSERIALIZE_CONTAINER(generic);
    fatal_if(generic.size() != num_generic,
             "%s: checkpoint has %d generic registers, not %d", name(),
             generic.size(), num_generic);
    for (unsigned n = 0; n < numContexts; ++n) {
        arrayParamIn(cp, csprintf("io%d", n), io[n]);
        fatal_if(io[n].size() != num_io,
                 "%s: checkpoint has %d job registers, not %d", name(),
                 io[n].size(), num_io);
    }
    UNSERIALIZE_SCALAR(pointerContext);
    UNSERIALIZE_SCALAR(runningContext);
    UNSERIALIZE_SCALAR(pending);
    UNSERIALIZE_SCALAR(running);
    UNSERIALIZE_SCALAR(acquired);
    UNSERIALIZE_SCALAR(jobId);
    UNSERIALIZE_SCALAR(doneJobs);
    UNSERIALIZE_SCALAR(finished);
    Tick start_tick, done_tick, irq_lower_tick;
    UNSERIALIZE_SCALAR(start_tick);
    UNSERIALIZE_SCALAR(done_tick);
    UNSERIALIZE_SCALAR(irq_lower_tick);
    if (start_tick != MaxTick)
        schedule(startEvent, start_tick);
    if (done_tick != MaxTick)
        schedule(doneEvent, done_tick);
    if (irq_lower_tick != MaxTick && irqPin) {
        // The IRQ was high at the checkpoint. Set it high again without
        // notifying the receiver, which restores its own state from its
        // own checkpoint section
        irqPin->set(true, true);
        schedule(irqLowerEvent, irq_lower_tick);
    }
    unserializeJob(cp);
    streamer.unserializeSection(cp, "streamer");
}

} // namespace gem5
