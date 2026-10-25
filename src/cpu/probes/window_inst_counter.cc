#include "cpu/probes/window_inst_counter.hh"

#include "arch/generic/isa.hh"
#include "base/bitfield.hh"
#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/WindowInstCounter.hh"
#include "enums/OpClass.hh"
#include "sim/byteswap.hh"

namespace gem5
{

WindowInstCounter::WindowInstCounter(const WindowInstCounterParams &p)
    : ProbeListenerObject(p),
      openPCs(p.open_pcs.begin(), p.open_pcs.end()),
      closePCs(p.close_pcs.begin(), p.close_pcs.end()),
      out(p.file, std::ios::out | std::ios::trunc),
      classes(!p.classes_file.empty()),
      names(!p.names_file.empty()),
      flips(!p.flips_file.empty()),
      activity(!p.activity_file.empty()),
      cpu(p.cpu),
      statNames(p.stats)
{
    fatal_if(openPCs.empty() || closePCs.empty(),
             "%s: needs at least one open and one close address", name());
    fatal_if(!out, "%s: cannot write %s", name(), p.file);
    if (classes) {
        classesOut.open(p.classes_file, std::ios::out | std::ios::trunc);
        fatal_if(!classesOut, "%s: cannot write %s", name(),
                 p.classes_file);
    }
    if (names) {
        namesOut.open(p.names_file, std::ios::out | std::ios::trunc);
        fatal_if(!namesOut, "%s: cannot write %s", name(), p.names_file);
    }
    if (flips) {
        fatal_if(!cpu, "%s: a flips file needs the cpu, to read registers",
                 name());
        flipsOut.open(p.flips_file, std::ios::out | std::ios::trunc);
        fatal_if(!flipsOut, "%s: cannot write %s", name(), p.flips_file);
    }
    if (activity) {
        activityOut.open(p.activity_file, std::ios::out | std::ios::trunc);
        fatal_if(!activityOut, "%s: cannot write %s", name(),
                 p.activity_file);
    }
    fatal_if(statNames.empty() != p.stats_file.empty(),
             "%s: stats and stats_file go together", name());
    if (!statNames.empty()) {
        statsOut.open(p.stats_file, std::ios::out | std::ios::trunc);
        fatal_if(!statsOut, "%s: cannot write %s", name(), p.stats_file);
    }
}

void
WindowInstCounter::startup()
{
    // The statistics exist by now; find each by its full name.
    for (const std::string &n : statNames) {
        const auto *info =
            dynamic_cast<const statistics::ScalarInfo *>(statistics::resolve(n));
        fatal_if(!info, "%s: no scalar statistic named %s", name(), n);
        statInfos.push_back(info);
    }
    statStart.assign(statInfos.size(), 0.0);
}

void
WindowInstCounter::regProbeListeners()
{
    typedef ProbeListenerArg<WindowInstCounter, BaseCPU::CommittedOp>
        Listener;
    connectListener<Listener>(this, "CommittedOps",
                              &WindowInstCounter::committed);
}

void
WindowInstCounter::committed(const BaseCPU::CommittedOp &op)
{
    const StaticInst &inst = *op.inst;
    if (counting && classes)
        ++opClasses[inst.opClass()];
    // getName() is not const; it only reads the mnemonic.
    if (counting && names && inst.isMicroop())
        ++opNames[const_cast<StaticInst &>(inst).getName()];
    if (counting) {
        DPRINTF(WindowInstCounter, "%#x %s: %s%s\n", op.pc,
                inst.disassemble(op.pc), enums::OpClassStrings[inst.opClass()],
                op.macro ? csprintf(" (macro-op %s: %s)",
                                    op.macro->disassemble(op.pc),
                                    enums::OpClassStrings[
                                        op.macro->opClass()])
                         : std::string());
    }
    if (counting && flips)
        countFlips(inst, op.macro ? *op.macro : inst);
    if (activity) {
        fatal_if(inst.isMicroop() && !op.macro,
                 "%s: the CPU does not report the macro-op of a micro-op, "
                 "which the activity file needs; use the atomic CPU",
                 name());
        const StaticInst &whole = op.macro ? *op.macro : inst;
        // In the order the execution trace shows them: an instruction that
        // is not split, with its access; a split one's macro-op before its
        // first micro-op, and each micro-op with its access.
        if (!inst.isMicroop()) {
            activityAccess(op, whole);
            activityInst(whole, op.pc);
        } else {
            if (inst.isFirstMicroop())
                activityInst(whole, op.pc);
            activityAccess(op, whole);
        }
    }
    if (inst.isMicroop() && !inst.isLastMicroop())
        return;

    // A whole instruction retired.
    const Addr pc = op.pc;
    if (counting) {
        ++count;
        fatal_if((classes || names) && inst.isMicroop() && !op.macro,
                 "%s: the CPU does not report the macro-op of a micro-op, "
                 "which the instruction classes and names need; use the "
                 "atomic CPU or neither file", name());
        const StaticInst *whole = op.macro ? op.macro : op.inst;
        if (classes)
            ++instClasses[whole->opClass()];
        if (names)
            ++instNames[const_cast<StaticInst *>(whole)->getName()];
    }
    if (closePCs.count(pc)) {
        if (counting) {
            out << count << '\n';
            out.flush();
            if (classes)
                writeClasses();
            if (names)
                writeNames();
            if (flips)
                writeFlips();
            if (activity)
                writeActivity();
            if (!statInfos.empty())
                writeStats();
            DPRINTF(WindowInstCounter, "window closes at %#x: %llu\n", pc,
                    (unsigned long long)count);
        } else {
            warn("%s: a window closes at %#x but none is open", name(), pc);
        }
        counting = false;
    } else if (openPCs.count(pc)) {
        if (counting)
            warn("%s: a window opens at %#x inside an open one", name(), pc);
        DPRINTF(WindowInstCounter, "window opens at %#x\n", pc);
        counting = true;
        count = 0;
        instClasses.fill(0);
        opClasses.fill(0);
        instNames.clear();
        opNames.clear();
        nameFlips.clear();
        if (flips)
            copyRegisters();
        for (size_t i = 0; i < statInfos.size(); ++i)
            statStart[i] = statInfos[i]->value();
    }
}

const WindowInstCounter::Encoded &
WindowInstCounter::encoded(const StaticInst &whole)
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
WindowInstCounter::activityInst(const StaticInst &whole, Addr pc)
{
    const Encoded &e = encoded(whole);
    const thumb::Kind kind = e.traits.kind;
    auto &c = activityCounts;
    if (activityOpen) {
        ++c[e.width == 2 ? 0 : 1];
        c[2] += e.traits.shift;
        c[3] += __builtin_popcount(e.enc ^ lastEnc);
        if (nextPCValid && pc != nextPC)
            ++c[7];
        if (numKinds && kind != kinds[numKinds - 1])
            ++c[8];
        bool seen = false;
        for (size_t i = 0; i < numKinds; ++i)
            seen = seen || kinds[i] == kind;
        if (!seen)
            ++c[9];
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
    // The same rule as the instruction count's window.
    if (closePCs.count(pc)) {
        activityOpen = false;
    } else if (openPCs.count(pc)) {
        activityOpen = true;
        c.fill(0);
        nextPCValid = false;
    }
}

void
WindowInstCounter::activityAccess(const BaseCPU::CommittedOp &op,
                                  const StaticInst &whole)
{
    if (!activityOpen || !op.effAddr)
        return;
    const Addr region = *op.effAddr >> 28;
    const bool write = encoded(whole).traits.write;
    if (region == 1 && !write)
        ++activityCounts[4];
    else if (region == 2)
        ++activityCounts[write ? 6 : 5];
}

void
WindowInstCounter::writeActivity()
{
    for (size_t i = 0; i < activityCounts.size(); ++i)
        activityOut << (i ? " " : "") << activityCounts[i];
    activityOut << '\n';
    activityOut.flush();
}

void
WindowInstCounter::copyRegisters()
{
    ThreadContext *tc = cpu->getContext(0);
    const BaseISA &isa = *tc->getIsaPtr();
    const RegClass &ints = *isa.regClasses().at(IntRegClass);
    copy.clear();
    // Keyed by the flat register, as getReg() reads it (Arm banks some).
    for (const RegId &id : ints)
        copy[id.flatten(isa).index()] = tc->getReg(id);
}

void
WindowInstCounter::countFlips(const StaticInst &inst, const StaticInst &whole)
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
        // Not in the copy: not written since the window opened, so as now.
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
    if (s || d) {
        auto &f = nameFlips[const_cast<StaticInst &>(whole).getName()];
        f.first += s;
        f.second += d;
    }
}

void
WindowInstCounter::writeStats()
{
    for (size_t i = 0; i < statInfos.size(); ++i) {
        statsOut << (i ? " " : "") << statNames[i] << '='
                 << statInfos[i]->value() - statStart[i];
    }
    statsOut << '\n';
    statsOut.flush();
}

void
WindowInstCounter::writeFlips()
{
    const char *sep = "";
    for (const auto &[n, f] : nameFlips) {
        if (f.first) {
            flipsOut << sep << "s." << n << '=' << f.first;
            sep = " ";
        }
    }
    for (const auto &[n, f] : nameFlips) {
        if (f.second) {
            flipsOut << sep << "d." << n << '=' << f.second;
            sep = " ";
        }
    }
    flipsOut << '\n';
    flipsOut.flush();
}

void
WindowInstCounter::writeClasses()
{
    const char *sep = "";
    for (int c = 0; c < Num_OpClasses; ++c) {
        if (instClasses[c]) {
            classesOut << sep << "inst_" << enums::OpClassStrings[c] << '='
                       << instClasses[c];
            sep = " ";
        }
    }
    for (int c = 0; c < Num_OpClasses; ++c) {
        if (opClasses[c]) {
            classesOut << sep << "op_" << enums::OpClassStrings[c] << '='
                       << opClasses[c];
            sep = " ";
        }
    }
    classesOut << '\n';
    classesOut.flush();
}

void
WindowInstCounter::writeNames()
{
    const char *sep = "";
    for (const auto &[n, count] : instNames) {
        namesOut << sep << "i." << n << '=' << count;
        sep = " ";
    }
    for (const auto &[n, count] : opNames) {
        namesOut << sep << "o." << n << '=' << count;
        sep = " ";
    }
    namesOut << '\n';
    namesOut.flush();
}

} // namespace gem5
