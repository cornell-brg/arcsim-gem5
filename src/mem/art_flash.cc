/**
 * @file
 * Implementation of an STM32 flash behind its accelerator (ART).
 */

#include "mem/art_flash.hh"

#include <algorithm>

namespace gem5
{

namespace memory
{

ARTFlash::ARTFlash(const ARTFlashParams &p) :
    SimpleMemory(p), readTime(p.read_time)
{
}

void
ARTFlash::insertResponse(PacketPtr pkt, Tick when)
{
    auto i = packetQueue.begin();
    while (i != packetQueue.end() && i->tick <= when)
        ++i;
    packetQueue.emplace(i, pkt, when);
    if (retryResp)
        return;
    Tick next = std::max(packetQueue.front().tick, curTick());
    if (dequeueEvent.scheduled())
        reschedule(dequeueEvent, next, true);
    else
        schedule(dequeueEvent, next);
}

bool
ARTFlash::recvTimingReq(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");
    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());

    const Tick now = curTick();
    const Tick receiveDelay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    const bool isPrefetch = pkt->req->isPrefetch();

    // The prefetch last scheduled, if its line has not been delivered.
    auto pfIt = packetQueue.end();
    if (pfPkt) {
        pfIt = std::find_if(packetQueue.begin(), packetQueue.end(),
                            [this](const DeferredPacket &d)
                            { return d.pkt == pfPkt; });
        if (pfIt == packetQueue.end() || pfIt->tick <= now) {
            pfPkt = nullptr;
            pfIt = packetQueue.end();
        }
    }

    Tick start;
    if (!isPrefetch && pfPkt && pkt->req->isInstFetch()) {
        // A fetch miss: the stream has moved, and the prefetch's read is
        // dropped. Its packet goes back when the fetch's line does, so
        // that the accelerator prefetches on from the new line.
        start = std::max(now, demandFreeAt);
        freeAt = demandFreeAt = start + readTime;
        PacketPtr pf = pfPkt;
        packetQueue.erase(pfIt);
        pfPkt = nullptr;
        insertResponse(pf, start + receiveDelay + getLatency());
    } else if (!isPrefetch && pfPkt && pfStart > now) {
        // A data read goes before a prefetch that has not started.
        start = std::max(now, demandFreeAt);
        demandFreeAt = start + readTime;
        PacketPtr pf = pfPkt;
        packetQueue.erase(pfIt);
        pfStart = demandFreeAt;
        freeAt = pfStart + readTime;
        insertResponse(pf, pfStart + pfRespOffset);
    } else {
        // Otherwise a read waits for the one in progress.
        start = std::max(now, freeAt);
        freeAt = start + readTime;
        if (!isPrefetch)
            demandFreeAt = freeAt;
    }

    const bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt);
    if (needsResponse) {
        assert(pkt->isResponse());
        const Tick offset = receiveDelay + getLatency();
        if (isPrefetch) {
            pfPkt = pkt;
            pfStart = start;
            pfRespOffset = offset;
        }
        insertResponse(pkt, start + offset);
    } else {
        pendingDelete.reset(pkt);
    }
    return true;
}

} // namespace memory
} // namespace gem5
