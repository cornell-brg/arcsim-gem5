#include "cpu/probes/window_inst_counter.hh"

#include <algorithm>

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
      classifier(p.classifier),
      flipsState(p.cpu),
      activityState(nullptr),
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
    fatal_if(p.latency_classes_file.empty() == !!classifier,
             "%s: latency_classes_file and classifier go together", name());
    if (classifier) {
        latencyClassesOut.open(p.latency_classes_file,
                               std::ios::out | std::ios::trunc);
        fatal_if(!latencyClassesOut, "%s: cannot write %s", name(),
                 p.latency_classes_file);
        std::map<std::string, size_t> named;
        for (const std::string &n : classifier->classNames()) {
            auto it = named.find(n);
            if (it == named.end()) {
                it = named.emplace(n, columns.size()).first;
                columns.push_back(n);
            }
            classColumn.push_back(it->second);
        }
        columns.push_back("other");
        latencyClasses.assign(columns.size(), 0);
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
    if (inst.isMemRef() && memClass == No_OpClass)
        memClass = inst.opClass();
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
        if (classifier) {
            fatal_if(inst.isMicroop() && !op.macro,
                     "%s: the CPU does not report the macro-op of a "
                     "micro-op, which the latency classes need; use the "
                     "atomic or Minor CPU", name());
            const int ci = classifier->classOf(*whole, memClass);
            ++latencyClasses[ci >= 0 ? classColumn[ci] : columns.size() - 1];
        }
    }
    memClass = No_OpClass;
    if (closePCs.count(pc)) {
        if (counting) {
            out << count << '\n';
            out.flush();
            if (classes)
                writeClasses();
            if (classifier)
                writeLatencyClasses();
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
        std::fill(latencyClasses.begin(), latencyClasses.end(), 0);
        nameFlips.clear();
        if (flips)
            flipsState.copyRegisters();
        for (size_t i = 0; i < statInfos.size(); ++i)
            statStart[i] = statInfos[i]->value();
    }
}

void
WindowInstCounter::activityInst(const StaticInst &whole, Addr pc)
{
    activityState.inst(whole, pc, activityOpen ? &activityCounts : nullptr);
    // The same rule as the instruction count's window.
    if (closePCs.count(pc)) {
        activityOpen = false;
    } else if (openPCs.count(pc)) {
        activityOpen = true;
        activityCounts.fill(0);
        activityState.forgetNextPC();
    }
}

void
WindowInstCounter::activityAccess(const BaseCPU::CommittedOp &op,
                                  const StaticInst &whole)
{
    if (!activityOpen)
        return;
    fatal_if(!activityState.access(op, whole, activityCounts),
             "%s: the CPU does not report data addresses, which the "
             "activity file needs; use the atomic CPU", name());
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
WindowInstCounter::countFlips(const StaticInst &inst, const StaticInst &whole)
{
    const auto [s, d] = flipsState.flips(inst);
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
WindowInstCounter::writeLatencyClasses()
{
    const char *sep = "";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (latencyClasses[i]) {
            latencyClassesOut << sep << "c." << columns[i] << '='
                              << latencyClasses[i];
            sep = " ";
        }
    }
    latencyClassesOut << '\n';
    latencyClassesOut.flush();
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
