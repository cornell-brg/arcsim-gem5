#include "arch/arm/m_inst_latency.hh"

#include <algorithm>
#include <map>

#include "arch/arm/m_faults.hh"
#include "base/bitfield.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/InstLatency.hh"
#include "enums/OpClass.hh"

namespace gem5
{

namespace ArmISA
{

namespace
{

// An ExtMachInst's Thumb bits: thumb (36) and bigThumb (35); a 16-bit
// instruction's halfword in bits 15:0, a 32-bit one's first halfword in
// bits 31:16 and its second below; the IT state in bits 55:48.
constexpr uint64_t Thumb = 1ULL << 36;
constexpr uint64_t BigThumb = 1ULL << 35;

unsigned
widthOf(uint64_t emi)
{
    return (emi & BigThumb) ? 4 : 2;
}

bool
inItBlock(uint64_t emi)
{
    return bits(emi, 51, 48) != 0;
}

// PUSH and POP, 16-bit and 32-bit (STMDB SP! and LDMIA SP!).
bool
isPushPop(uint64_t emi)
{
    const uint64_t t16 = emi & (Thumb | BigThumb | 0xFE00);
    const uint64_t t32 = emi & (Thumb | BigThumb | 0xFFFF0000);
    return t16 == (Thumb | 0xB400) || t16 == (Thumb | 0xBC00) ||
        t32 == (Thumb | BigThumb | 0xE92D0000) ||
        t32 == (Thumb | BigThumb | 0xE8BD0000);
}

OpClass
opClassNamed(const std::string &name)
{
    for (int i = 0; i < enums::Num_OpClass; ++i) {
        if (name == enums::OpClassStrings[i])
            return static_cast<OpClass>(i);
    }
    fatal("MProfileInstLatency: no OpClass named %s", name);
}

// The aligned transfers on a 32-bit bus that an access of size bytes at
// addr takes: a read, the words it touches; a write, the naturally aligned
// bytes, halfwords and words it splits into (as MinorCPU's
// executeAlignedTransfers).
unsigned
transfersOf(Addr addr, unsigned size, bool write)
{
    if (size == 0)
        return 0;
    if (!write)
        return ((addr + size - 1) >> 2) - (addr >> 2) + 1;
    unsigned n = 0;
    Addr at = addr;
    const Addr end = addr + size;
    while (at < end) {
        unsigned piece = 4;
        while (piece > 1 && ((at % piece) != 0 || at + piece > end))
            piece /= 2;
        at += piece;
        ++n;
    }
    return n;
}

} // anonymous namespace

MProfileLatencyClass::MProfileLatencyClass(const Params &p)
    : SimObject(p), description(p.description), cycles(p.cycles),
      resultDelay(p.result_delay), unit(p.unit), busy(p.busy),
      divide(p.divide), accumulate(p.accumulate),
      branchExtra(p.branch_extra), mask(p.mask), match(p.match),
      srcLats(p.src_lats.begin(), p.src_lats.end()), minSrcLat(0)
{
    if (!p.op_classes.empty()) {
        opClasses.assign(enums::Num_OpClass, false);
        for (const auto &name : p.op_classes)
            opClasses[opClassNamed(name)] = true;
    }
    if (!srcLats.empty())
        minSrcLat = *std::min_element(srcLats.begin(), srcLats.end());
}

bool
MProfileLatencyClass::takes(OpClass op_class, uint64_t emi) const
{
    return (opClasses.empty() || opClasses[op_class]) &&
        (emi & mask) == match;
}

bool
MProfileLatencyClass::late(int i) const
{
    if (srcLats.empty())
        return false;
    const Cycles lat = i < (int)srcLats.size() ? srcLats[i] : srcLats.back();
    return lat > minSrcLat;
}

MProfileInstLatency::ClassStats::ClassStats(statistics::Group *parent,
                                            const std::string &name)
    : statistics::Group(parent, name.c_str()),
      ADD_STAT(insts, statistics::units::Count::get(),
               "instructions of the class"),
      ADD_STAT(cycles, statistics::units::Cycle::get(),
               "their cycles, waits and memory included")
{
}

MProfileInstLatency::LatencyStats::LatencyStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(insts, statistics::units::Count::get(),
               "instructions retired"),
      ADD_STAT(cycles, statistics::units::Cycle::get(),
               "their cycles, exception entries included"),
      ADD_STAT(memoryCycles, statistics::units::Cycle::get(),
               "cycles memory accesses added"),
      ADD_STAT(resultWaits, statistics::units::Cycle::get(),
               "cycles instructions waited for a result"),
      ADD_STAT(unitWaits, statistics::units::Cycle::get(),
               "cycles instructions waited for their unit"),
      ADD_STAT(folded, statistics::units::Count::get(),
               "instructions folded with the one before or after"),
      ADD_STAT(takenBranches, statistics::units::Count::get(),
               "taken branches"),
      ADD_STAT(unalignedTargets, statistics::units::Count::get(),
               "32-bit branch targets at a halfword address"),
      ADD_STAT(entries, statistics::units::Count::get(),
               "exception entries"),
      ADD_STAT(returns, statistics::units::Count::get(),
               "exception returns"),
      ADD_STAT(tailChains, statistics::units::Count::get(),
               "returns straight into another exception")
{
}

MProfileInstLatency::MProfileInstLatency(const Params &p)
    : BaseInstLatency(p), classes(p.classes),
      defaultCycles(p.default_cycles),
      foldMasks(p.fold_masks), foldMatches(p.fold_matches),
      foldKinds(p.fold_kinds.begin(), p.fold_kinds.end()),
      takenBranch(p.taken_branch), notTakenBranch(p.not_taken_branch),
      branchFromLoad(p.branch_from_load),
      unalignedTarget(p.unaligned_target),
      entryCycles(p.exception_entry), returnCycles(p.exception_return),
      tailChainCycles(p.tail_chain), fetchAhead(p.fetch_ahead),
      timedRanges(p.timed_ranges), waitRanges(p.wait_ranges),
      waitCycles(p.wait_cycles.begin(), p.wait_cycles.end()),
      waitWriteCycles(p.wait_write_cycles.empty() ? waitCycles
          : std::vector<Cycles>(p.wait_write_cycles.begin(),
                                p.wait_write_cycles.end())),
      stats(this)
{
    fatal_if(foldMasks.size() != foldMatches.size() ||
             foldMasks.size() != foldKinds.size(),
             "%s: fold_masks, fold_matches and fold_kinds differ in length",
             name());
    fatal_if(waitRanges.size() != waitCycles.size() ||
             waitRanges.size() != waitWriteCycles.size(),
             "%s: wait_ranges, wait_cycles and wait_write_cycles differ in "
             "length", name());
    // Classes that share a name (one per pattern) share their statistics.
    std::map<std::string, ClassStats *> named;
    for (const MProfileLatencyClass *c : classes) {
        fatal_if(c->unit >= (int)unitFree.size(),
                 "%s: class %s's unit %d is not below %d", name(),
                 c->description, c->unit, unitFree.size());
        auto it = named.find(c->description);
        if (it == named.end()) {
            ownStats.push_back(
                std::make_unique<ClassStats>(this, c->description));
            it = named.emplace(c->description, ownStats.back().get()).first;
        }
        classStats.push_back(it->second);
    }
    auto it = named.find("other");
    if (it == named.end()) {
        ownStats.push_back(std::make_unique<ClassStats>(this, "other"));
        otherStats = ownStats.back().get();
    } else {
        otherStats = it->second;
    }
}

MProfileInstLatency::Reg
MProfileInstLatency::regKey(const RegId &reg)
{
    return (static_cast<uint32_t>(reg.classValue()) << 16) | reg.index();
}

int
MProfileInstLatency::classify(OpClass op_class, uint64_t emi) const
{
    for (size_t i = 0; i < classes.size(); ++i) {
        if (classes[i]->takes(op_class, emi))
            return i;
    }
    return -1;
}

unsigned
MProfileInstLatency::foldKind(uint64_t emi) const
{
    for (size_t i = 0; i < foldMasks.size(); ++i) {
        if ((emi & foldMasks[i]) == foldMatches[i])
            return foldKinds[i];
    }
    return 0;
}

void
MProfileInstLatency::clearInFlight()
{
    hasMemOp = false;
    transfers = 0;
    readsMemory = false;
    control = false;
    operandCycles = Cycles(0);
    sources.clear();
    results.clear();
    writesFlags = false;
}

void
MProfileInstLatency::forgetPrevious()
{
    havePrevious = false;
    itPending = false;
    branchedTo = false;
    aheadPending = false;
    clearInFlight();
}

Cycles
MProfileInstLatency::exceptionEntry(ThreadContext *tc)
{
    Cycles c = entryCycles;
    if (returnPending) {
        c = tailChainCycles;
        returnPending = false;
        ++stats.tailChains;
    }
    ++stats.entries;
    stats.cycles += c;
    forgetPrevious();
    DPRINTF(InstLatency, "exception entry: %d cycles\n", (uint64_t)c);
    return c;
}

Cycles
MProfileInstLatency::faultTaken(const Fault &fault, ThreadContext *tc)
{
    // Whatever the faulting fetch or instruction did is not charged.
    memoryCycles = Cycles(0);
    forgetPrevious();
    if (dynamic_cast<const MProfileExcReturnFetch *>(fault.get())) {
        // Charged to the next instruction, or folded into the next entry.
        returnPending = true;
        return Cycles(0);
    }
    ++stats.entries;
    stats.cycles += entryCycles;
    DPRINTF(InstLatency, "%s: %d cycles\n", fault->name(),
            (uint64_t)entryCycles);
    return entryCycles;
}

void
MProfileInstLatency::beforeOp(const StaticInstPtr &inst,
                              const StaticInstPtr &macro, ThreadContext *tc)
{
    const BaseISA &isa = *tc->getIsaPtr();
    const uint64_t emi = inst->getEMI();
    inFlightStore = inst->isStore();
    // The class of this micro-op, as a MinorCPU, which runs micro-ops,
    // would take it: which of its sources it reads late.
    const int ci = classify(inst->opClass(), emi);
    // A multiply-accumulate's accumulate operands, from its encoding: Ra in
    // bits 15:12 of the second halfword, and a long multiply's (first
    // halfword 1111 1011 1xxx) RdHi in bits 11:8 [RP2350 datasheet
    // 3.7.4.9.3]. MUL's Ra field is 1111.
    uint32_t late_regs = 0;
    if (ci >= 0 && classes[ci]->accumulate && widthOf(emi) == 4) {
        const unsigned ra = bits(emi, 15, 12);
        if (ra != 0xf)
            late_regs |= 1u << ra;
        if (bits(emi, 31, 23) == 0x1f7)
            late_regs |= 1u << bits(emi, 11, 8);
    }
    for (int i = 0; i < inst->numSrcRegs(); ++i) {
        const RegId reg = inst->srcRegIdx(i).flatten(isa);
        if (!reg.isRenameable() || reg.is(CCRegClass))
            continue;
        const bool late = (ci >= 0 && classes[ci]->late(i)) ||
            (reg.is(IntRegClass) && reg.index() < 32 &&
             ((late_regs >> reg.index()) & 1));
        sources.push_back({regKey(reg), late});
    }

    if (inst->opClass() == IntDivOp && inst->numSrcRegs() >= 2) {
        // As MinorCPU's RP2350 M33 divider (DynamicLatencyIntDivFU): Thumb's
        // divide decodes its divisor before its dividend, and the cycles
        // follow the distance between their most significant ones.
        const bool is_signed = inst->getName() == "sdiv";
        auto magnitude = [is_signed](uint32_t raw) {
            if (!is_signed)
                return raw;
            const int64_t value = static_cast<int32_t>(raw);
            return static_cast<uint32_t>(value < 0 ? -value : value);
        };
        const uint32_t divisor = magnitude(tc->getReg(inst->srcRegIdx(0)));
        const uint32_t dividend = magnitude(tc->getReg(inst->srcRegIdx(1)));
        unsigned latency = is_signed ? 4 : 2;
        if (dividend && divisor) {
            const int difference =
                __builtin_clz(divisor) - __builtin_clz(dividend);
            if (difference >= 0)
                latency = 4 + difference / 4;
        }
        operandCycles = Cycles(latency);
    }
}

void
MProfileInstLatency::afterOp(const StaticInstPtr &inst,
                             const StaticInstPtr &macro, ThreadContext *tc)
{
    const BaseISA &isa = *tc->getIsaPtr();
    control = control || inst->isControl();
    const int ci = classify(inst->opClass(), inst->getEMI());
    const bool load = inst->isLoad();
    if (inst->isMemRef() && !hasMemOp) {
        hasMemOp = true;
        memOpClass = inst->opClass();
    }
    readsMemory = readsMemory || load;
    // A load's registers come one a bus transfer, in order; this one's
    // transfers have been counted already.
    unsigned transfer = 0;
    unsigned loaded = 0;
    for (int i = 0; i < inst->numDestRegs(); ++i) {
        const RegId reg = inst->destRegIdx(i).flatten(isa);
        if (reg.is(CCRegClass)) {
            writesFlags = true;
            continue;
        }
        if (!reg.isRenameable())
            continue;
        if (load)
            ++loaded;
        results.push_back({regKey(reg), ci, load, 0});
    }
    if (load && loaded) {
        // The registers of this load: the last of its transfers for the
        // last one, the ones before for the others.
        const unsigned last = transfers ? transfers - 1 : 0;
        transfer = last + 1 >= loaded ? last + 1 - loaded : 0;
        for (auto it = results.end() - loaded; it != results.end(); ++it)
            it->transfer = std::min(transfer++, last);
    }
}

void
MProfileInstLatency::access(Addr paddr, unsigned size, Cycles latency,
                            bool fetch)
{
    if (!fetch)
        transfers += transfersOf(paddr, size, inFlightStore);
    for (const AddrRange &range : timedRanges) {
        if (range.contains(paddr)) {
            memoryCycles += latency;
            return;
        }
    }
    for (size_t i = 0; i < waitRanges.size(); ++i) {
        if (waitRanges[i].contains(paddr)) {
            memoryCycles += !fetch && inFlightStore ? waitWriteCycles[i]
                                                    : waitCycles[i];
            return;
        }
    }
}

bool
MProfileInstLatency::fetchedAhead(Addr &addr)
{
    if (!aheadPending)
        return false;
    aheadPending = false;
    addr = aheadAddr;
    return true;
}

Cycles
MProfileInstLatency::retire(const StaticInstPtr &whole, const PCStateBase &pc,
                            const PCStateBase &next, Cycles now,
                            ThreadContext *tc)
{
    const uint64_t emi = whole->getEMI();
    const unsigned width = widthOf(emi);
    const Addr at = pc.instAddr();
    const bool taken = next.instAddr() != at + width;
    const bool sequential = havePrevious && at == prevNext;
    // Whether this instruction can fold with the one before
    // [RP2350 datasheet 3.7.4.9.7, 3.7.4.9.9].
    const bool can_fold = havePrevious && prevWidth == 2 && !prevFolded &&
        (prevSequential || (prevPC & 3) == 0);

    const OpClass op_class = hasMemOp ? memOpClass : whole->opClass();
    const int ci = classify(op_class, emi);
    const MProfileLatencyClass *cls = ci >= 0 ? classes[ci] : nullptr;
    const unsigned fold = foldKind(emi);

    // Its own cycles
    Cycles c = cls ? cls->cycles : defaultCycles;
    if (cls && cls->divide && operandCycles != 0)
        c = operandCycles;
    // A load or store, a cycle a transfer on the bus: one a register,
    // two for a word that crosses a word boundary.
    if (transfers > 1)
        c += Cycles(transfers - 1);

    bool folded = false;
    bool branched_to = false;
    if (control || taken) {
        const bool fold_with_previous = fold == 1 && can_fold;
        // A branch a load makes takes the load's time first; any other's
        // basic time is none.
        const Cycles basic = readsMemory ? c : Cycles(0);
        if (taken) {
            const bool conditional = whole->isCondCtrl() || inItBlock(emi);
            const bool known = !(conditional && prevWritesFlags);
            c = basic + takenBranch;
            if (readsMemory)
                c += branchFromLoad;
            if (cls)
                c += cls->branchExtra;
            if (known && fold_with_previous) {
                c = Cycles(c - 1);
                folded = true;
            }
            branched_to = !prevPushPop;
            ++stats.takenBranches;
            if (fetchAhead) {
                // The word after the one the branch ends in
                aheadPending = true;
                aheadAddr = ((at + width - 1) & ~Addr(3)) + 4;
            }
        } else {
            c = basic + notTakenBranch;
            if (fold_with_previous && c > 0) {
                c = Cycles(c - 1);
                folded = true;
            }
        }
    } else if (fold == 1 && can_fold) {
        // A NOP after a 16-bit instruction
        c = Cycles(0);
        folded = true;
    } else if (fold == 2) {
        // An IT, which takes a cycle only before a 32-bit instruction
        c = Cycles(0);
    }

    // When it issues: after an IT before it and a target at a halfword
    // address, once the results it reads are ready, and once its unit is
    // free.
    Cycles issue = now;
    if (itPending) {
        if (width == 4)
            issue += Cycles(1);
        else
            folded = true;
    }
    if (branchedTo && width == 4 && (at & 2)) {
        issue += unalignedTarget;
        ++stats.unalignedTargets;
    }
    Cycles needed(0);
    for (const Source &s : sources) {
        auto it = ready.find(s.reg);
        if (it == ready.end())
            continue;
        const Cycles r = s.late && it->second > 0 ? Cycles(it->second - 1)
                                                  : it->second;
        needed = std::max(needed, r);
    }
    if (needed > issue) {
        stats.resultWaits += needed - issue;
        issue = needed;
    }
    if (cls && cls->unit >= 0) {
        Cycles &free = unitFree[cls->unit];
        if (free > issue) {
            stats.unitWaits += free - issue;
            issue = free;
        }
        free = issue + cls->busy;
    }
    const Cycles memory = memoryCycles;
    memoryCycles = Cycles(0);
    stats.memoryCycles += memory;
    c = Cycles((issue - now) + c + memory);
    if (returnPending) {
        c += returnCycles;
        returnPending = false;
        ++stats.returns;
    }

    // When its results are ready: a load's after its transfer, a unit's
    // after the unit is done, any other's after its cycles, each with its
    // class's result delay.
    for (const Result &r : results) {
        const MProfileLatencyClass *rc = r.cls >= 0 ? classes[r.cls] : cls;
        const Cycles delay = rc ? rc->resultDelay : Cycles(0);
        Cycles at_ready;
        if (r.loaded)
            at_ready = issue + memory + Cycles(r.transfer + 1) + delay;
        else if (rc && rc->unit >= 0)
            at_ready = issue + rc->busy + delay;
        else
            at_ready = now + c + delay;
        ready[r.reg] = at_ready;
    }

    ClassStats *s = ci >= 0 ? classStats[ci] : otherStats;
    ++s->insts;
    s->cycles += c;
    ++stats.insts;
    stats.cycles += c;
    stats.folded += folded;
    DPRINTF(InstLatency, "%#x %s: %s, %d cycles (issue +%d, memory %d)%s\n",
            at, whole->disassemble(at), cls ? cls->description : "other",
            (uint64_t)c, (uint64_t)(issue - now), (uint64_t)memory,
            folded ? ", folded" : "");

    // This becomes the instruction before.
    havePrevious = true;
    prevPC = at;
    prevNext = at + width;
    prevWidth = width;
    prevFolded = folded;
    prevSequential = sequential;
    prevWritesFlags = writesFlags;
    prevPushPop = isPushPop(emi);
    itPending = fold == 2;
    branchedTo = branched_to;
    clearInFlight();
    return c;
}

} // namespace ArmISA
} // namespace gem5
