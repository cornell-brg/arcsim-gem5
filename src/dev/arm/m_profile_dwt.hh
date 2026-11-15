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

#ifndef __DEV_ARM_M_PROFILE_DWT_HH__
#define __DEV_ARM_M_PROFILE_DWT_HH__

/**
 * @file
 * M-profile Data Watchpoint and Trace unit (DWT), CYCCNT only.
 *
 * The 4KB block at 0xE0001000 (DDI0403E C1.8).  CYCCNT is derived
 * lazily from simulated time: no event fires per cycle.  While the
 * counter runs, CYCCNT = base + cycles since startTick; while it is
 * stopped, CYCCNT = base.  The counter runs only while both
 * DEMCR.TRCENA (owned by the SCS) and DWT_CTRL.CYCCNTENA are set,
 * as on silicon.  The SCS reports TRCENA changes through
 * traceEnableChanged() so an edge latches or freezes the count at
 * the right tick.
 */

#include <cstdint>

#include "dev/io_device.hh"
#include "params/MProfileDWT.hh"

namespace gem5
{

class MProfileSCS;

class MProfileDWT : public BasicPioDevice
{
  public:
    PARAMS(MProfileDWT);
    MProfileDWT(const Params &p);

    // Registers with the SCS so DEMCR.TRCENA writes reach this device.
    void init() override;

    // Called by the SCS when DEMCR.TRCENA changes value.
    void traceEnableChanged(bool enabled);

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

  private:
    // Register offsets within the block (DDI0403E Table C1-23).
    enum : Addr
    {
        CTRL     = 0x000,
        CYCCNT   = 0x004,
        CPICNT   = 0x008,
        EXCCNT   = 0x00C,
        SLEEPCNT = 0x010,
        LSUCNT   = 0x014,
        FOLDCNT  = 0x018,
        PCSR     = 0x01C,
    };

    // DWT_CTRL layout.  NUMCOMP is read-only; the implementation
    // reports four comparators like the Cortex-M4 even though none
    // is modeled.  NOCYCCNT and NOPRFCNT read as zero (present).
    static constexpr uint32_t CTRL_CYCCNTENA = 1u << 0;
    static constexpr uint32_t CTRL_NUMCOMP = 4u << 28;
    // Writable bits: CYCEVTENA..EXCTRCENA [22:16], PCSAMPLENA [12],
    // SYNCTAP, CYCTAP, POSTINIT, POSTPRESET and CYCCNTENA [11:0].
    static constexpr uint32_t CTRL_WRITE_MASK = 0x007F1FFF;

    uint32_t readReg(Addr offset);
    void writeReg(Addr offset, uint32_t data);

    bool running() const;
    uint32_t currentCount() const;
    // Latches startTick or freezes base when the running state changes.
    void updateRunning(bool wasRunning);

    MProfileSCS *scs;

    uint32_t ctrl = 0;
    // CYCCNT value at the last freeze or write.
    uint32_t base = 0;
    // Tick at which the counter last started running from base.
    Tick startTick = 0;
    // Cached DEMCR.TRCENA, kept current by traceEnableChanged().
    bool traceEnabled = false;
};

} // namespace gem5

#endif // __DEV_ARM_M_PROFILE_DWT_HH__
