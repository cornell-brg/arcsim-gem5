#include "arch/riscv/inst_latency.hh"

#include <algorithm>
#include <map>

#include "arch/riscv/pcstate.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/e906_divsqrt.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/RiscvInstLatency.hh"
#include "enums/OpClass.hh"

namespace gem5
{

namespace RiscvISA
{

namespace
{

OpClass
opClassNamed(const std::string &name)
{
    for (int i = 0; i < enums::Num_OpClass; ++i) {
        if (name == enums::OpClassStrings[i])
            return static_cast<OpClass>(i);
    }
    fatal("RiscvInstLatency: no OpClass named %s", name);
}

} // anonymous namespace

RiscvLatencyClass::RiscvLatencyClass(const Params &p)
    : SimObject(p), description(p.description), cycles(p.cycles),
      resultDelay(p.result_delay), unit(p.unit), busy(p.busy),
      mask(p.mask), match(p.match),
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
RiscvLatencyClass::takes(OpClass op_class, uint64_t emi) const
{
    return (opClasses.empty() || opClasses[op_class]) &&
        (emi & mask) == match;
}

bool
RiscvLatencyClass::late(int i) const
{
    if (srcLats.empty())
        return false;
    const Cycles lat = i < (int)srcLats.size() ? srcLats[i] : srcLats.back();
    return lat > minSrcLat;
}

RiscvInstLatency::ClassStats::ClassStats(statistics::Group *parent,
                                         const std::string &name)
    : statistics::Group(parent, name.c_str()),
      ADD_STAT(insts, statistics::units::Count::get(),
               "instructions of the class"),
      ADD_STAT(cycles, statistics::units::Cycle::get(),
               "their cycles, waits and memory included")
{
}

RiscvInstLatency::LatencyStats::LatencyStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(insts, statistics::units::Count::get(),
               "instructions retired"),
      ADD_STAT(cycles, statistics::units::Cycle::get(),
               "their cycles, trap entries included"),
      ADD_STAT(memoryCycles, statistics::units::Cycle::get(),
               "cycles memory accesses added"),
      ADD_STAT(resultWaits, statistics::units::Cycle::get(),
               "cycles instructions waited for a result"),
      ADD_STAT(unitWaits, statistics::units::Cycle::get(),
               "cycles instructions waited for their unit"),
      ADD_STAT(takenBranches, statistics::units::Count::get(),
               "taken branches and jumps"),
      ADD_STAT(predictedBranches, statistics::units::Count::get(),
               "taken branches the predictor predicted"),
      ADD_STAT(mispredictedBranches, statistics::units::Count::get(),
               "branches predicted taken that were not"),
      ADD_STAT(unalignedTargets, statistics::units::Count::get(),
               "32-bit targets at a halfword address"),
      ADD_STAT(entries, statistics::units::Count::get(), "trap entries")
{
}

RiscvInstLatency::RiscvInstLatency(const Params &p)
    : BaseInstLatency(p), classes(p.classes),
      defaultCycles(p.default_cycles), takenBranch(p.taken_branch),
      notTakenBranch(p.not_taken_branch),
      predictedBranch(p.predicted_branch), predictor(p.predictor),
      unalignedTarget(p.unaligned_target), entryCycles(p.trap_entry),
      e906DivSqrt(p.e906_div_sqrt),
      timedRanges(p.timed_ranges), waitRanges(p.wait_ranges),
      waitCycles(p.wait_cycles.begin(), p.wait_cycles.end()),
      waitWriteCycles(p.wait_write_cycles.empty() ? waitCycles
          : std::vector<Cycles>(p.wait_write_cycles.begin(),
                                p.wait_write_cycles.end())),
      stats(this)
{
    fatal_if(waitRanges.size() != waitCycles.size() ||
             waitRanges.size() != waitWriteCycles.size(),
             "%s: wait_ranges, wait_cycles and wait_write_cycles differ in "
             "length", name());
    // Classes that share a name share their statistics.
    std::map<std::string, ClassStats *> named;
    for (const RiscvLatencyClass *c : classes) {
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

RiscvInstLatency::Reg
RiscvInstLatency::regKey(const RegId &reg)
{
    return (static_cast<uint32_t>(reg.classValue()) << 16) | reg.index();
}

int
RiscvInstLatency::classify(OpClass op_class, uint64_t emi) const
{
    for (size_t i = 0; i < classes.size(); ++i) {
        if (classes[i]->takes(op_class, emi))
            return i;
    }
    return -1;
}

std::vector<std::string>
RiscvInstLatency::classNames() const
{
    std::vector<std::string> names;
    for (const RiscvLatencyClass *c : classes)
        names.push_back(c->description);
    return names;
}

int
RiscvInstLatency::classOf(const StaticInst &whole, OpClass mem_class) const
{
    // As retire() classes it.
    return classify(mem_class != No_OpClass ? mem_class : whole.opClass(),
                    whole.getEMI());
}

void
RiscvInstLatency::clearInFlight()
{
    hasMemOp = false;
    control = false;
    ops = 0;
    operandCycles = Cycles(0);
    sources.clear();
    results.clear();
}

Cycles
RiscvInstLatency::exceptionEntry(ThreadContext *tc)
{
    ++stats.entries;
    stats.cycles += entryCycles;
    branchedTo = false;
    clearInFlight();
    DPRINTF(RiscvInstLatency, "interrupt entry: %d cycles\n",
            (uint64_t)entryCycles);
    return entryCycles;
}

Cycles
RiscvInstLatency::faultTaken(const Fault &fault, ThreadContext *tc)
{
    // ecall, ebreak or a fault: the jump to mtvec [instruction_timings.adoc]
    memoryCycles = Cycles(0);
    branchedTo = false;
    clearInFlight();
    ++stats.entries;
    stats.cycles += entryCycles;
    DPRINTF(RiscvInstLatency, "%s: %d cycles\n", fault->name(),
            (uint64_t)entryCycles);
    return entryCycles;
}

void
RiscvInstLatency::beforeOp(const StaticInstPtr &inst,
                           const StaticInstPtr &macro, ThreadContext *tc)
{
    const BaseISA &isa = *tc->getIsaPtr();
    inFlightStore = inst->isStore();
    const OpClass op = inst->opClass();
    if (e906DivSqrt && (op == FloatDivOp || op == FloatSqrtOp) &&
        inst->numSrcRegs() >= 1) {
        // Its operands before it runs: its cycles follow them
        const bool sqrt = op == FloatSqrtOp;
        const uint32_t a = tc->getReg(inst->srcRegIdx(0));
        const uint32_t b = !sqrt && inst->numSrcRegs() > 1 ?
            uint32_t(tc->getReg(inst->srcRegIdx(1))) : 0;
        operandCycles = Cycles(e906DivSqrtCycles(sqrt, a, b));
    }
    const int ci = classify(inst->opClass(), inst->getEMI());
    for (int i = 0; i < inst->numSrcRegs(); ++i) {
        const RegId reg = inst->srcRegIdx(i).flatten(isa);
        if (!reg.isRenameable() || (reg.is(IntRegClass) && reg.index() == 0))
            continue;
        sources.push_back({regKey(reg), ci >= 0 && classes[ci]->late(i)});
    }
}

void
RiscvInstLatency::afterOp(const StaticInstPtr &inst,
                          const StaticInstPtr &macro, ThreadContext *tc)
{
    const BaseISA &isa = *tc->getIsaPtr();
    ++ops;
    control = control || inst->isControl();
    if (inst->isMemRef() && !hasMemOp) {
        hasMemOp = true;
        memOpClass = inst->opClass();
    }
    const int ci = classify(inst->opClass(), inst->getEMI());
    for (int i = 0; i < inst->numDestRegs(); ++i) {
        const RegId reg = inst->destRegIdx(i).flatten(isa);
        if (!reg.isRenameable() || (reg.is(IntRegClass) && reg.index() == 0))
            continue;
        results.push_back({regKey(reg), ci});
    }
}

void
RiscvInstLatency::access(Addr paddr, unsigned size, Cycles latency,
                         bool fetch)
{
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

Cycles
RiscvInstLatency::retire(const StaticInstPtr &whole, const PCStateBase &pc,
                         const PCStateBase &next, Cycles now,
                         ThreadContext *tc)
{
    const Addr at = pc.instAddr();
    const unsigned width = pc.as<PCState>().compressed() ? 2 : 4;
    const bool taken = next.instAddr() != at + width;

    const OpClass op_class = hasMemOp ? memOpClass : whole->opClass();
    const int ci = classify(op_class, whole->getEMI());
    const RiscvLatencyClass *cls = ci >= 0 ? classes[ci] : nullptr;

    // Its own cycles; a split instruction (Zcmp's push and pop, say) a
    // cycle more for each micro-op after the first.
    Cycles c = operandCycles != Cycles(0) ? operandCycles
             : cls ? cls->cycles : defaultCycles;
    const Cycles extra_ops(ops > 1 ? ops - 1 : 0);

    bool branched_to = false;
    if (control || taken) {
        // Hazard3's predictor: the last taken backward branch is predicted
        // taken when it comes again, and forgotten when it is not taken
        // [instruction_timings.adoc, Branch Predictor].
        const bool conditional = whole->isCondCtrl();
        const bool predicted = predictor && conditional && havePredicted &&
            predictedPC == at;
        if (taken) {
            if (predicted) {
                c = predictedBranch;
                ++stats.predictedBranches;
            } else {
                c = takenBranch;
                if (predictor && conditional && next.instAddr() < at) {
                    havePredicted = true;
                    predictedPC = at;
                }
            }
            branched_to = true;
            ++stats.takenBranches;
        } else if (predicted) {
            c = takenBranch;
            havePredicted = false;
            ++stats.mispredictedBranches;
        } else {
            c = notTakenBranch;
        }
    }
    c += extra_ops;

    // When it issues: a cycle late at a 32-bit target at a halfword
    // address, then once the results it reads are ready, and once its unit
    // is free.
    Cycles issue = now;
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

    // Its results are ready after its cycles and its class's result delay.
    for (const Result &r : results) {
        const RiscvLatencyClass *rc = r.cls >= 0 ? classes[r.cls] : cls;
        ready[r.reg] = now + c + (rc ? rc->resultDelay : Cycles(0));
    }

    ClassStats *s = ci >= 0 ? classStats[ci] : otherStats;
    ++s->insts;
    s->cycles += c;
    ++stats.insts;
    stats.cycles += c;
    DPRINTF(RiscvInstLatency, "%#x %s: %s, %d cycles (issue +%d, memory %d)\n",
            at, whole->disassemble(at), cls ? cls->description : "other",
            (uint64_t)c, (uint64_t)(issue - now), (uint64_t)memory);

    branchedTo = branched_to;
    clearInFlight();
    return c;
}

} // namespace RiscvISA
} // namespace gem5
