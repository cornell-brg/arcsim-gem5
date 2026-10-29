#include "dev/hwpe/hwpe_replay.hh"

#include <deque>
#include <fstream>
#include <set>
#include <sstream>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "mem/packet_access.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

namespace
{

// Hex digits as little-endian bytes, the last digits the first byte
std::vector<uint8_t>
hexBytes(const std::string &hex)
{
    std::vector<uint8_t> bytes;
    for (size_t end = hex.size(); end >= 2; end -= 2)
        bytes.push_back(std::stoul(hex.substr(end - 2, 2), nullptr, 16));
    return bytes;
}

} // anonymous namespace

bool
HWPEReplay::ReqPort::recvTimingResp(PacketPtr pkt)
{
    delete pkt;
    return true;
}

HWPEReplay::HWPEReplay(const Params &p)
    : ClockedObject(p), hwpeBase(p.hwpe_base), memBase(p.mem_base),
      hwpePort(name() + ".hwpe_port"), memPort(name() + ".mem_port"),
      irq(name() + ".irq", 0, this),
      accessEvent([this] { issue(); }, name() + ".access")
{
    parse(p.trace);
}

Port &
HWPEReplay::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "hwpe_port")
        return hwpePort;
    if (if_name == "mem_port")
        return memPort;
    if (if_name == "irq")
        return irq;
    return ClockedObject::getPort(if_name, idx);
}

void
HWPEReplay::parse(const std::string &path)
{
    std::ifstream in(path);
    fatal_if(!in, "%s: cannot open %s", name(), path);
    uint64_t done_cycle = 0;
    std::deque<Addr> reads;
    std::set<Addr> written;     // by the engine since the last done
    std::set<Addr> loaded;
    loads.emplace_back();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        uint64_t cycle;
        std::string kind;
        if (!(s >> cycle >> kind))
            continue;
        if (kind == "done") {
            ++totalDones;
            done_cycle = cycle;
            loads.emplace_back();
            written.clear();
            loaded.clear();
        } else if (kind == "trigger") {
            accesses.push_back({totalDones, cycle - done_cycle, true, 0, 0});
        } else if (kind == "wreg" || kind == "rreg") {
            std::string reg, data = "0";
            s >> reg;
            if (kind == "wreg")
                s >> data;
            accesses.push_back({totalDones, cycle - done_cycle,
                                kind == "wreg", std::stoul(reg, nullptr, 16),
                                uint32_t(std::stoul(data, nullptr, 16))});
        } else if (kind == "read") {
            std::string addr;
            s >> addr;
            reads.push_back(std::stoull(addr, nullptr, 16));
        } else if (kind == "rdata") {
            std::string data;
            s >> data;
            fatal_if(reads.empty(), "%s: %s: rdata without a read", name(),
                     path);
            const Addr addr = reads.front();
            reads.pop_front();
            if (!written.count(addr) && loaded.insert(addr).second)
                loads.back().push_back({addr, hexBytes(data)});
        } else if (kind == "write") {
            std::string addr;
            s >> addr;
            written.insert(std::stoull(addr, nullptr, 16));
        }
    }
    DPRINTF(HWPE, "replay: %d register accesses, %d jobs\n",
            accesses.size(), totalDones);
}

void
HWPEReplay::load(unsigned n)
{
    if (n >= loads.size())
        return;
    for (const Word &w : loads[n]) {
        auto req = std::make_shared<Request>(memBase + w.addr, w.data.size(),
                                             0, Request::funcRequestorId);
        Packet pkt(req, MemCmd::WriteReq);
        pkt.dataStatic(w.data.data());
        memPort.sendFunctional(&pkt);
    }
}

void
HWPEReplay::startup()
{
    load(0);
    schedule_next();
}

void
HWPEReplay::schedule_next()
{
    if (nextAccess == accesses.size() ||
        accesses[nextAccess].dones > dones)
        return;
    const Tick target =
        lastDone + cyclesToTicks(Cycles(accesses[nextAccess].offset));
    reschedule(accessEvent, target >= curTick() ? target : clockEdge(),
               true);
}

void
HWPEReplay::issue()
{
    const Access &a = accesses[nextAccess++];
    auto req = std::make_shared<Request>(hwpeBase + a.reg, 4, 0,
                                         Request::funcRequestorId);
    PacketPtr pkt = new Packet(req, a.write ? MemCmd::WriteReq :
                                              MemCmd::ReadReq);
    pkt->allocate();
    if (a.write)
        pkt->setLE<uint32_t>(a.data);
    panic_if(!hwpePort.sendTimingReq(pkt), "%s: the HWPE refused a register "
             "access", name());
    schedule_next();
}

void
HWPEReplay::raiseInterruptPin(int)
{
    ++dones;
    lastDone = clockEdge();
    DPRINTF(HWPE, "replay: done %d of %d\n", dones, totalDones);
    if (dones == totalDones) {
        exitSimLoop("HWPEReplay: every job done");
        return;
    }
    load(dones);
    schedule_next();
}

} // namespace gem5
