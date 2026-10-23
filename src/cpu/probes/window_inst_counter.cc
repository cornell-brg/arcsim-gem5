#include "cpu/probes/window_inst_counter.hh"

#include "arch/generic/isa.hh"
#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/WindowInstCounter.hh"
#include "enums/OpClass.hh"

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
