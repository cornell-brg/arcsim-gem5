/*
 * Copyright (c) 2026 University of California, Davis and Cornell University
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

#ifndef __ARCH_ARM_M_SYSTEM_HH__
#define __ARCH_ARM_M_SYSTEM_HH__

/**
 * @file
 * M-profile (ARMv7-M / ARMv8-M) system object.
 *
 * Inherits directly from System (not ArmSystem) because M-profile
 * has no exception levels, GIC, Generic Timer, SVE/SME, or AArch64
 * state.  The only shared component with A-profile is ArmRelease
 * (extension checking), which is stored as a direct member.
 */

#include <array>

#include "base/bitfield.hh"
#include "base/logging.hh"
#include "enums/ArmExtension.hh"
#include "params/ArmMSystem.hh"
#include "sim/system.hh"

namespace gem5
{

class ArmRelease;
class ArmSemihosting;
class MProfileCoprocessor;
class MProfileSCS;

class ArmMSystem : public System
{
  protected:
    const ArmRelease *release;

    /** Each core's SCS/NVIC device, by context id, set by its
     *  MProfileSCS's constructor (its core parameter). */
    std::array<MProfileSCS *, 2> scs{};

    /** Coprocessors on the coprocessor port, by number (p0-p7); nullptr
     *  where none is attached. */
    std::array<MProfileCoprocessor *, 8> coprocessors{};

    /** CPUID register value (DDI0403E B3.2.3).
     *  Set from Python param (provided by platform).  Identifies the
     *  core variant to firmware via SCB->CPUID at 0xE000ED00. */
    const uint32_t _cpuid;

  public:
    /** Semihosting handler, or nullptr if disabled. */
    ArmSemihosting *const semihosting;

    PARAMS(ArmMSystem);
    ArmMSystem(const Params &p);

    const ArmRelease *releaseFS() const { return release; }

    /** CPUID register value for this core variant. */
    uint32_t getCPUID() const { return _cpuid; }

    bool has(ArmExtension ext) const;

    /** Whether semihosting is enabled. */
    bool haveSemihosting() const { return semihosting != nullptr; }

    /** M-profile physical address range is always 32 bits. */
    uint8_t physAddrRange() const { return 32; }

    /** M-profile physical address mask (32-bit). */
    Addr physAddrMask() const { return mask(32); }

    /** Called by each MProfileSCS's constructor to register it as its
     *  core's SCS device. */
    void
    setSCS(unsigned core, MProfileSCS *s)
    {
        fatal_if(core >= scs.size(), "an SCS for core %u: at most %u cores",
                 core, (unsigned)scs.size());
        fatal_if(scs[core], "two SCS devices for core %u", core);
        scs[core] = s;
    }

    /** The SCS device of the core with context id `core`, or nullptr. */
    MProfileSCS *
    getSCS(unsigned core) const
    {
        return core < scs.size() ? scs[core] : nullptr;
    }

    /** Called by each MProfileCoprocessor for the numbers it answers. */
    void
    setCoprocessor(unsigned n, MProfileCoprocessor *c)
    {
        fatal_if(coprocessors.at(n), "coprocessor p%u attached twice", n);
        coprocessors[n] = c;
    }

    /** The coprocessor attached as p<n> (n < 8), or nullptr. */
    MProfileCoprocessor *getCoprocessor(unsigned n) const
    {
        return coprocessors.at(n);
    }
};

} // namespace gem5

#endif // __ARCH_ARM_M_SYSTEM_HH__
