#include "cpu/testers/rp2350_sram/rp2350_sram_test.hh"

#include <algorithm>

#include "base/logging.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

namespace gem5
{
RP2350SRAMTest::TestPort::TestPort(RP2350SRAMTest &owner, PortID id)
    : RequestPort(owner.name() + ".port[" + std::to_string(id) + "]", id),
      test(owner)
{}

bool
RP2350SRAMTest::TestPort::recvTimingResp(PacketPtr pkt)
{
    return test.response(pkt, id);
}

void
RP2350SRAMTest::TestPort::send()
{
    while (!pending.empty()) {
        if (!sendTimingReq(pending.front()))
            return;
        pending.pop_front();
    }
}

void
RP2350SRAMTest::TestPort::recvReqRetry()
{
    panic_if(pending.empty(), "Spurious SRAM request retry");
    send();
}

RP2350SRAMTest::RP2350SRAMTest(const RP2350SRAMTestParams &p)
    : ClockedObject(p), scenario(p.scenario), sramWindow(p.sram_window),
      sramLatency(p.sram_latency),
      requestor(p.system->getRequestorId(this)),
      stepEvent([this] { step(); }, name() + ".step"),
      retryEvent([this] { ports[0]->sendRetryResp(); }, name() + ".retry")
{
    fatal_if(p.port_port_connection_count != 2, "Test needs two manager ports");
    for (PortID id = 0; id < 2; ++id)
        ports.emplace_back(std::make_unique<TestPort>(*this, id));
}

Port &
RP2350SRAMTest::getPort(const std::string &name, PortID id)
{
    if (name == "port")
        return *ports.at(id);
    return ClockedObject::getPort(name, id);
}

PacketPtr
RP2350SRAMTest::packet(Addr addr, unsigned size, bool write,
                     const std::vector<uint8_t> &data, Tick due)
{
    auto req = std::make_shared<Request>(addr, size, Request::Flags(), requestor);
    auto pkt = new Packet(req, write ? MemCmd::WriteReq : MemCmd::ReadReq);
    auto bytes = new uint8_t[size]();
    if (write)
        std::copy(data.begin(), data.end(), bytes);
    pkt->dataDynamic(bytes);
    auto expected = new Expected();
    expected->data = data;
    expected->due = due;
    pkt->senderState = expected;
    return pkt;
}

void
RP2350SRAMTest::startup()
{
    schedule(stepEvent, clockEdge(Cycles(1)));
}

void
RP2350SRAMTest::step()
{
    if (scenario != "data" && scenario != "atomic") {
        start = curTick();
        for (unsigned id = 0; id < 2; ++id) {
            if (id == 1 && (scenario == "split" || scenario == "backpressure"))
                continue;
            for (unsigned i = 0; i < 4; ++i) {
                unsigned size = (scenario == "split" || scenario == "backpressure") ? 8 : 4;
                if (scenario == "byte") size = 1;
                if (scenario == "half") size = 2;
                Addr addr = 0x20000000 + (scenario == "different" ? id * 4 : 0);
                // The cycle, counted from this one, in which the packet's
                // last beat is granted; its response follows within it.
                int cycle = -1;
                if (scenario == "same" || scenario == "byte" || scenario == "half")
                    cycle = 2 * i + id;
                if (scenario == "different") cycle = i;
                if (scenario == "priority") cycle = i + (id == 0 ? 4 : 0);
                if (scenario == "split") cycle = i;
                ports[id]->pending.push_back(packet(addr, size, false,
                    std::vector<uint8_t>(size, 0),
                    cycle >= 0 ? start + cycle * clockPeriod() + sramWindow +
                                 sramLatency : 0));
            }
        }
        // Reverse callback order to ensure arbitration does not depend on
        // which manager's sendTimingReq callback happens first.
        ports[1]->send();
        ports[0]->send();
        return;
    }

    PacketPtr pkt;
    if (scenario == "atomic") {
        std::vector<uint8_t> old = phase == 0 ? std::vector<uint8_t>(4, 0) :
                                              std::vector<uint8_t>(4, 0x11);
        if (phase == 0 || phase == 3 || phase == 5) {
            pkt = packet(0x20000000, 4, false,
                         phase == 5 ? std::vector<uint8_t>(4, 0x55) : old);
            if (phase == 0) {
                pkt->req->setFlags(Request::LLSC);
                pkt->cmd = MemCmd::LoadLockedReq;
            }
        } else {
            pkt = packet(0x20000000, 4, true,
                         std::vector<uint8_t>(4, phase == 1 ? 0x11 :
                                                 phase == 2 ? 0x22 : 0x55));
            if (phase == 4) {
                pkt->req->setFlags(Request::MEM_SWAP);
                pkt->cmd = MemCmd::SwapReq;
                static_cast<Expected *>(pkt->senderState)->data = old;
            } else {
                pkt->req->setFlags(Request::LLSC);
                pkt->cmd = MemCmd::StoreCondReq;
            }
        }
        pkt->req->setContext(0);
        ports[0]->pending.push_back(pkt);
        ports[0]->send();
        return;
    }
    if (phase == 0) {
        std::vector<uint8_t> initial(32);
        for (unsigned i = 0; i < initial.size(); ++i) initial[i] = i;
        auto init = packet(0x20000000, 32, true, initial);
        ports[0]->sendFunctional(init);
        delete init->senderState;
        delete init;
        auto atomic = packet(0x20000003, 8, false, {});
        Tick latency = ports[0]->sendAtomic(atomic);
        panic_if(latency != sramLatency, "Wrong atomic-mode latency");
        for (unsigned i = 0; i < 8; ++i)
            panic_if(atomic->getConstPtr<uint8_t>()[i] != i + 3, "Atomic read mismatch");
        delete atomic->senderState;
        delete atomic;
        std::vector<uint8_t> data(8);
        for (unsigned i = 0; i < 8; ++i) data[i] = 0xa0 + i;
        pkt = packet(0x20000002, 8, true, data);
        pkt->req->setByteEnable({true, false, true, false, true, false, true, false});
    } else if (phase == 1) {
        std::vector<uint8_t> data(8);
        for (unsigned i = 0; i < 8; ++i) data[i] = i % 2 ? i + 2 : 0xa0 + i;
        pkt = packet(0x20000002, 8, false, data);
    } else if (phase == 2 || phase == 3) {
        // Split across the SRAM8/SRAM9 boundary.
        pkt = packet(0x20080ffc, 8, phase == 2,
                     {0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7});
    } else if (phase == 4) {
        pkt = packet(0x20000010, 8, true,
                     {0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7});
    } else {
        pkt = packet(0x20000010, 8, false,
                     {0xb0, 0xb1, 0xb2, 0xff, 0xb4, 0xb5, 0xb6, 0xb7});
    }
    ports[0]->pending.push_back(pkt);
    ports[0]->send();
    if (phase == 4) {
        auto read = packet(0x20000010, 8, false, {});
        ports[0]->sendFunctional(read);
        for (unsigned i = 0; i < 8; ++i)
            panic_if(read->getConstPtr<uint8_t>()[i] != 0xb0 + i,
                     "Functional read did not see queued write");
        delete read->senderState;
        delete read;
        auto write = packet(0x20000012, 2, true, {0xee, 0xff});
        write->req->setByteEnable({false, true});
        ports[0]->sendFunctional(write);
        delete write->senderState;
        delete write;
    }
}

bool
RP2350SRAMTest::response(PacketPtr pkt, PortID id)
{
    if (scenario == "backpressure" && !rejected) {
        rejected = true;
        schedule(retryEvent, clockEdge(Cycles(3)));
        return false;
    }
    auto expected = static_cast<Expected *>(pkt->senderState);
    panic_if(pkt->isError(), "SRAM error response");
    if (scenario == "atomic" && (phase == 1 || phase == 2))
        panic_if(pkt->req->getExtraData() != (phase == 1 ? 1 : 0),
                 "Wrong SRAM store-conditional success result");
    if (pkt->isRead()) {
        panic_if(expected->data.size() != pkt->getSize(), "Invalid test expectation");
        for (unsigned i = 0; i < pkt->getSize(); ++i)
            panic_if(pkt->getConstPtr<uint8_t>()[i] != expected->data[i],
                     "SRAM data mismatch in %s phase %u byte %u", scenario, phase, i);
    }
    panic_if(expected->due && expected->due != curTick(),
             "SRAM %s port %d response at %llu, expected %llu",
             scenario, id, curTick(), expected->due);
    delete expected;
    delete pkt;
    ++received;
    if ((scenario == "data" || scenario == "atomic") && ++phase < 6) {
        schedule(stepEvent, clockEdge(Cycles(1)));
    } else if (((scenario == "data" || scenario == "atomic") && phase == 6) ||
               received == ((scenario == "split" || scenario == "backpressure") ? 4 : 8)) {
        exitSimLoop("RP2350_SRAM_TEST_PASS " + scenario);
    }
    return true;
}
} // namespace gem5
