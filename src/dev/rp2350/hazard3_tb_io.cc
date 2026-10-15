#include "dev/rp2350/hazard3_tb_io.hh"

#include <cstdio>
#include <string>

#include "base/cprintf.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Hazard3TbIO.hh"
#include "mem/packet_access.hh"
#include "sim/serialize.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

Hazard3TbIO::Hazard3TbIO(const Params &p) : BasicPioDevice(p, SIZE) {}

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
    // Every register this model has is write-only.
    const Addr offset = pkt->getAddr() - pioAddr;
    checkAccess(pkt, offset);
    unmodelled(pkt, offset);
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
}

void
Hazard3TbIO::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(exited);
}

} // namespace gem5
