#include "cpu/probes/inst_activity.hh"

#include "arch/generic/isa.hh"
#include "base/bitfield.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "sim/byteswap.hh"

namespace gem5
{

const char *const InstActivity::countNames[10] = {
    "n16", "n32", "shift", "iflip", "rdflash", "rdsram", "wrsram", "taken",
    "kswitch", "kfirst4"};

const InstActivity::Encoded &
InstActivity::encoded(const StaticInst &whole)
{
    // asBytes() is not const; it only reads the machine instruction.
    uint64_t mach = 0;
    const_cast<StaticInst &>(whole).asBytes(&mach, sizeof(mach));
    mach = letoh(mach);
    const unsigned width = whole.size();
    auto it = encodings.find(mach);
    if (it == encodings.end()) {
        // Arm's machine instruction: the encoding in the low bits, the IT
        // state (condition, mask) in bits 55-48.
        const uint32_t enc = mach & mask(width * 8);
        const uint64_t itstate = bits(mach, 55, 48);
        const int cond = bits(itstate, 3, 0) ? (int)bits(itstate, 7, 4) : -1;
        it = encodings.emplace(
            mach, Encoded{enc, width, thumb::traits(enc, width, cond)}).first;
    }
    return it->second;
}

void
InstActivity::inst(const StaticInst &whole, Addr pc, Counts *c)
{
    const Encoded &e = encoded(whole);
    const thumb::Kind kind = e.traits.kind;
    if (c) {
        ++(*c)[e.width == 2 ? 0 : 1];
        (*c)[2] += e.traits.shift;
        (*c)[3] += __builtin_popcount(e.enc ^ lastEnc);
        if (nextPCValid && pc != nextPC)
            ++(*c)[7];
        if (numKinds && kind != kinds[numKinds - 1])
            ++(*c)[8];
        bool seen = false;
        for (size_t i = 0; i < numKinds; ++i)
            seen = seen || kinds[i] == kind;
        if (!seen)
            ++(*c)[9];
    }
    if (numKinds == kinds.size()) {
        for (size_t i = 1; i < kinds.size(); ++i)
            kinds[i - 1] = kinds[i];
        --numKinds;
    }
    kinds[numKinds++] = kind;
    lastEnc = e.enc;
    nextPC = pc + e.width;
    nextPCValid = true;
}

bool
InstActivity::access(const BaseCPU::CommittedOp &op, const StaticInst &whole,
                     Counts &c)
{
    // A memory reference whose condition fails (in an IT block) makes no
    // access and has no address; a CPU that reports none has none for the
    // first memory references either.
    if (op.inst->isMemRef()) {
        if (op.effAddr)
            ++addressed;
        else if (++unaddressed >= 64 && addressed == 0)
            return false;
    }
    if (!op.effAddr)
        return true;
    const Addr region = *op.effAddr >> 28;
    const bool write = encoded(whole).traits.write;
    if (region == 1 && !write)
        ++c[4];
    else if (region == 2)
        ++c[write ? 6 : 5];
    return true;
}

void
InstActivity::copyRegisters()
{
    ThreadContext *tc = cpu->getContext(0);
    const BaseISA &isa = *tc->getIsaPtr();
    const RegClass &ints = *isa.regClasses().at(IntRegClass);
    copy.clear();
    // Keyed by the flat register, as getReg() reads it (Arm banks some).
    for (const RegId &id : ints)
        copy[id.flatten(isa).index()] = tc->getReg(id);
}

std::pair<uint64_t, uint64_t>
InstActivity::flips(const StaticInst &inst)
{
    ThreadContext *tc = cpu->getContext(0);
    const BaseISA &isa = *tc->getIsaPtr();
    const OpClass oc = inst.opClass();
    auto bits = [](RegVal a, RegVal b) {
        return (uint64_t)__builtin_popcountll((a ^ b) & 0xffffffffULL);
    };
    uint64_t s = 0, d = 0;
    int slot = 0;
    for (int i = 0; i < inst.numSrcRegs() && slot < Slots; ++i) {
        const RegId &id = inst.srcRegIdx(i);
        if (!id.is(IntRegClass))
            continue;
        const RegIndex flat = id.flatten(isa).index();
        auto it = copy.find(flat);
        // Not in the copy: not written since it was taken, so as now.
        const RegVal v = it != copy.end() ? it->second : tc->getReg(id);
        s += bits(v, lastSrc[oc][slot]);
        lastSrc[oc][slot++] = v;
    }
    slot = 0;
    for (int i = 0; i < inst.numDestRegs(); ++i) {
        const RegId &id = inst.destRegIdx(i);
        if (!id.is(IntRegClass))
            continue;
        const RegVal v = tc->getReg(id);
        copy[id.flatten(isa).index()] = v;
        if (slot < Slots) {
            d += bits(v, lastDest[oc][slot]);
            lastDest[oc][slot++] = v;
        }
    }
    return {s, d};
}

} // namespace gem5
