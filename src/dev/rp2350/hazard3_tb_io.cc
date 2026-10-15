#include "dev/rp2350/hazard3_tb_io.hh"

#include <cstdio>
#include <string>

#include "base/bitfield.hh"
#include "base/cprintf.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Hazard3TbIO.hh"
#include "mem/packet_access.hh"
#include "sim/serialize.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

Hazard3TbIO::Hazard3TbIO(const Params &p)
    : BasicPioDevice(p, SIZE),
      timerEvent([this] { updateTimer(); }, name() + ".timer")
{
    fatal_if(p.port_irq_connection_count > 32,
             "Hazard3TbIO: %d IRQ pins connected; SET_IRQ has 32 bits\n",
             p.port_irq_connection_count);
    for (int n = 0; n < p.port_irq_connection_count; ++n) {
        irqPins.emplace_back(std::make_unique<IntSourcePin<Hazard3TbIO>>(
            csprintf("%s.irq[%d]", name(), n), n, this));
    }
    if (p.port_soft_irq_connection_count) {
        softIrqPin = std::make_unique<IntSourcePin<Hazard3TbIO>>(
            name() + ".soft_irq", 0, this);
    }
    if (p.port_timer_irq_connection_count) {
        timerIrqPin = std::make_unique<IntSourcePin<Hazard3TbIO>>(
            name() + ".timer_irq", 0, this);
    }
}

Port &
Hazard3TbIO::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "irq") {
        fatal_if(idx < 0 || static_cast<size_t>(idx) >= irqPins.size(),
                 "Hazard3TbIO: no IRQ pin %d\n", idx);
        return *irqPins[idx];
    }
    if (if_name == "soft_irq" && softIrqPin)
        return *softIrqPin;
    if (if_name == "timer_irq" && timerIrqPin)
        return *timerIrqPin;
    return BasicPioDevice::getPort(if_name, idx);
}

void
Hazard3TbIO::startup()
{
    BasicPioDevice::startup();
    // As from rtl-sim's reset: the timer IRQ is up while mtime >= mtimecmp0,
    // both 0.
    driveIrqs();
    updateTimer();
}

uint64_t
Hazard3TbIO::mtime() const
{
    return mtimeOffset + curCycle();
}

void
Hazard3TbIO::setMtime(uint64_t value)
{
    mtimeOffset = value - curCycle();
}

void
Hazard3TbIO::driveIrqs()
{
    for (size_t n = 0; n < irqPins.size(); ++n) {
        const bool level = bits(irqState, n);
        if (level == bool(bits(irqDriven, n)))
            continue;
        if (level)
            irqPins[n]->raise();
        else
            irqPins[n]->lower();
        irqDriven = insertBits(irqDriven, n, level);
    }
    const bool soft = bits(softIrqState, 0);
    if (softIrqPin && soft != softIrqDriven) {
        if (soft)
            softIrqPin->raise();
        else
            softIrqPin->lower();
        softIrqDriven = soft;
    }
}

void
Hazard3TbIO::updateTimer()
{
    const uint64_t now = mtime();
    const bool level = now >= mtimecmp[0];
    if (timerIrqPin && level != timerIrqDriven) {
        if (level)
            timerIrqPin->raise();
        else
            timerIrqPin->lower();
        timerIrqDriven = level;
    }
    if (timerEvent.scheduled())
        deschedule(timerEvent);
    if (!level)
        schedule(timerEvent, clockEdge(Cycles(mtimecmp[0] - now)));
}

const char *
Hazard3TbIO::regName(Addr offset)
{
    switch (offset) {
      case PRINT_CHAR: return "PRINT_CHAR";
      case PRINT_U32: return "PRINT_U32";
      case EXIT: return "EXIT";
      case SET_SOFTIRQ: return "SET_SOFTIRQ";
      case CLR_SOFTIRQ: return "CLR_SOFTIRQ";
      case SET_IRQ: return "SET_IRQ";
      case CLR_IRQ: return "CLR_IRQ";
      case MTIME: return "MTIME";
      case MTIMEH: return "MTIMEH";
      case MTIMECMP0: return "MTIMECMP0";
      case MTIMECMP0H: return "MTIMECMP0H";
      case MTIMECMP1: return "MTIMECMP1";
      case MTIMECMP1H: return "MTIMECMP1H";
      default: return nullptr;
    }
}

namespace
{

// The PC of the access that made pkt, or "unknown" (e.g. a functional
// access from the loader or a debugger).
std::string
pcString(PacketPtr pkt)
{
    return pkt->req->hasPC() ? csprintf("%#x", pkt->req->getPC())
                             : std::string("unknown");
}

} // anonymous namespace

void
Hazard3TbIO::checkAccess(PacketPtr pkt, Addr offset) const
{
    if (pkt->getSize() != 4 || (offset & 3)) {
        fatal("Hazard3TbIO: %d-byte %s at offset 0x%03x (pc %s); only "
              "aligned 32-bit accesses are modelled\n", pkt->getSize(),
              pkt->isWrite() ? "write" : "read", offset, pcString(pkt));
    }
}

void
Hazard3TbIO::unmodelled(PacketPtr pkt, Addr offset) const
{
    const char *op = pkt->isWrite() ? "write" : "read";
    const char *name = regName(offset);
    if (!pkt->isWrite() &&
        (offset == PRINT_CHAR || offset == PRINT_U32 || offset == EXIT)) {
        fatal("Hazard3TbIO: read of write-only %s (offset 0x%03x, pc %s)\n",
              name, offset, pcString(pkt));
    }
    if (name) {
        fatal("Hazard3TbIO: %s of %s (offset 0x%03x, pc %s) is not "
              "modelled\n", op, name, offset, pcString(pkt));
    }
    fatal("Hazard3TbIO: %s at offset 0x%03x (pc %s): no register\n",
          op, offset, pcString(pkt));
}

Tick
Hazard3TbIO::read(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;
    checkAccess(pkt, offset);

    uint32_t data;
    switch (offset) {
      case SET_SOFTIRQ:
      case CLR_SOFTIRQ:
        data = softIrqState;
        break;
      case SET_IRQ:
      case CLR_IRQ:
        data = irqState;
        break;
      case MTIME: data = bits(mtime(), 31, 0); break;
      case MTIMEH: data = bits(mtime(), 63, 32); break;
      case MTIMECMP0: data = bits(mtimecmp[0], 31, 0); break;
      case MTIMECMP0H: data = bits(mtimecmp[0], 63, 32); break;
      case MTIMECMP1: data = bits(mtimecmp[1], 31, 0); break;
      case MTIMECMP1H: data = bits(mtimecmp[1], 63, 32); break;
      default:
        // PRINT_CHAR, PRINT_U32 and EXIT are write-only.
        unmodelled(pkt, offset);
    }
    pkt->setLE<uint32_t>(data);
    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
Hazard3TbIO::write(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;
    checkAccess(pkt, offset);
    const uint32_t data = pkt->getLE<uint32_t>();

    switch (offset) {
      case PRINT_CHAR:
        // As rtl-sim: the low byte, unbuffered so it interleaves with
        // gem5's own output.
        std::printf("%c", static_cast<char>(data & 0xff));
        std::fflush(stdout);
        break;
      case PRINT_U32:
        std::printf("%08x\n", data);
        std::fflush(stdout);
        break;
      case EXIT:
        DPRINTF(Hazard3TbIO, "EXIT %#x (pc %s)%s\n", data, pcString(pkt),
                exited ? ", ignored" : "");
        if (!exited) {
            exited = true;
            // A signed code, as rtl-sim prints it: tb_exit(-1) exits -1.
            const int code = static_cast<int32_t>(data);
            exitSimLoop(csprintf("CPU requested halt. Exit code %d", code),
                        code);
        }
        break;
      case SET_SOFTIRQ:
        softIrqState |= data;
        driveIrqs();
        break;
      case CLR_SOFTIRQ:
        softIrqState &= ~data;
        driveIrqs();
        break;
      case SET_IRQ:
        irqState |= data;
        driveIrqs();
        break;
      case CLR_IRQ:
        irqState &= ~data;
        driveIrqs();
        break;
      case MTIME:
        setMtime(insertBits(mtime(), 31, 0, data));
        updateTimer();
        break;
      case MTIMEH:
        setMtime(insertBits(mtime(), 63, 32, data));
        updateTimer();
        break;
      case MTIMECMP0:
        mtimecmp[0] = insertBits(mtimecmp[0], 31, 0, data);
        updateTimer();
        break;
      case MTIMECMP0H:
        mtimecmp[0] = insertBits(mtimecmp[0], 63, 32, data);
        updateTimer();
        break;
      case MTIMECMP1:
        mtimecmp[1] = insertBits(mtimecmp[1], 31, 0, data);
        break;
      case MTIMECMP1H:
        mtimecmp[1] = insertBits(mtimecmp[1], 63, 32, data);
        break;
      default:
        unmodelled(pkt, offset);
    }
    pkt->makeAtomicResponse();
    return pioDelay;
}

void
Hazard3TbIO::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(exited);
    SERIALIZE_SCALAR(irqState);
    SERIALIZE_SCALAR(softIrqState);
    const uint64_t mtime_now = mtime();
    SERIALIZE_SCALAR(mtime_now);
    SERIALIZE_ARRAY(mtimecmp, 2);
}

void
Hazard3TbIO::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(exited);
    UNSERIALIZE_SCALAR(irqState);
    UNSERIALIZE_SCALAR(softIrqState);
    uint64_t mtime_now;
    UNSERIALIZE_SCALAR(mtime_now);
    setMtime(mtime_now);
    UNSERIALIZE_ARRAY(mtimecmp, 2);
}

} // namespace gem5
