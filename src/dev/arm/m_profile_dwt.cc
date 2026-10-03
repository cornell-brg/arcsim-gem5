/*
 * Copyright (c) 2026 Cornell University
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dev/arm/m_profile_dwt.hh"

#include "debug/MProfileDWT.hh"
#include "dev/arm/m_profile_scs.hh"
#include "mem/packet_access.hh"
#include "sim/serialize.hh"

namespace gem5
{

MProfileDWT::MProfileDWT(const Params &p)
    : BasicPioDevice(p, 0x1000),  // 4KB DWT block
      scs(p.scs)
{
}

void
MProfileDWT::init()
{
    BasicPioDevice::init();
    scs->attachDWT(this);
    traceEnabled = scs->traceEnabled();
}

bool
MProfileDWT::running() const
{
    return traceEnabled && (ctrl & CTRL_CYCCNTENA);
}

uint32_t
MProfileDWT::currentCount() const
{
    if (!running())
        return base;
    // Unsigned arithmetic wraps CYCCNT at 2^32 like the hardware.
    return base + (uint32_t)ticksToCycles(curTick() - startTick);
}

void
MProfileDWT::updateRunning(bool wasRunning)
{
    bool nowRunning = running();
    if (nowRunning && !wasRunning) {
        startTick = curTick();
        DPRINTF(MProfileDWT, "CYCCNT starts at %u (tick %llu)\n",
                base, (unsigned long long)startTick);
    } else if (!nowRunning && wasRunning) {
        base += (uint32_t)ticksToCycles(curTick() - startTick);
        DPRINTF(MProfileDWT, "CYCCNT freezes at %u (tick %llu)\n",
                base, (unsigned long long)curTick());
    }
}

void
MProfileDWT::traceEnableChanged(bool enabled)
{
    bool wasRunning = running();
    traceEnabled = enabled;
    updateRunning(wasRunning);
}

uint32_t
MProfileDWT::readReg(Addr offset)
{
    switch (offset) {
      case CTRL:
        return (ctrl & CTRL_WRITE_MASK) | CTRL_NUMCOMP;
      case CYCCNT:
        return currentCount();
      default:
        // Profiling counters, PCSR, comparators and the ID space:
        // not modeled, read as zero.
        return 0;
    }
}

void
MProfileDWT::writeReg(Addr offset, uint32_t data)
{
    switch (offset) {
      case CTRL: {
        bool wasRunning = running();
        ctrl = data & CTRL_WRITE_MASK;
        updateRunning(wasRunning);
        break;
      }
      case CYCCNT:
        // A write restarts the count from the written value.
        base = data;
        startTick = curTick();
        DPRINTF(MProfileDWT, "CYCCNT <- %u\n", base);
        break;
      default:
        // CPICNT..FOLDCNT and everything else: writes are accepted
        // and have no effect.
        break;
    }
}

Tick
MProfileDWT::read(PacketPtr pkt)
{
    Addr daddr = pkt->getAddr() - pioAddr;
    Addr alignedAddr = daddr & ~0x3;
    unsigned size = pkt->getSize();

    uint32_t data = readReg(alignedAddr);
    DPRINTF(MProfileDWT, "read offset %#x size %u -> %#x\n",
            daddr, size, data);

    switch (size) {
      case 1: {
        int byteOffset = daddr & 0x3;
        pkt->setLE<uint8_t>((data >> (byteOffset * 8)) & 0xFF);
        break;
      }
      case 2: {
        int byteOffset = daddr & 0x2;
        pkt->setLE<uint16_t>((data >> (byteOffset * 8)) & 0xFFFF);
        break;
      }
      default:
        pkt->setLE<uint32_t>(data);
        break;
    }

    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
MProfileDWT::write(PacketPtr pkt)
{
    Addr daddr = pkt->getAddr() - pioAddr;
    Addr alignedAddr = daddr & ~0x3;
    unsigned size = pkt->getSize();

    // Sub-word writes merge into the aligned register word.
    uint32_t data;
    if (size < 4) {
        int byteOffset = daddr & 0x3;
        uint32_t existing = readReg(alignedAddr);
        uint32_t mask, value;
        if (size == 1) {
            mask = 0xFFu << (byteOffset * 8);
            value = (uint32_t)pkt->getLE<uint8_t>() << (byteOffset * 8);
        } else {
            byteOffset &= 0x2;
            mask = 0xFFFFu << (byteOffset * 8);
            value = (uint32_t)pkt->getLE<uint16_t>() << (byteOffset * 8);
        }
        data = (existing & ~mask) | value;
    } else {
        data = pkt->getLE<uint32_t>();
    }

    DPRINTF(MProfileDWT, "write offset %#x size %u <- %#x\n",
            daddr, size, data);
    writeReg(alignedAddr, data);

    pkt->makeAtomicResponse();
    return pioDelay;
}

void
MProfileDWT::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(ctrl);
    SERIALIZE_SCALAR(base);
    SERIALIZE_SCALAR(startTick);
    SERIALIZE_SCALAR(traceEnabled);
}

void
MProfileDWT::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(ctrl);
    UNSERIALIZE_SCALAR(base);
    UNSERIALIZE_SCALAR(startTick);
    UNSERIALIZE_SCALAR(traceEnabled);
}

} // namespace gem5
