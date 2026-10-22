#include "cpu/probes/window_inst_counter.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/static_inst.hh"
#include "debug/WindowInstCounter.hh"
#include "enums/OpClass.hh"

namespace gem5
{

WindowInstCounter::WindowInstCounter(const WindowInstCounterParams &p)
    : ProbeListenerObject(p),
      openPCs(p.open_pcs.begin(), p.open_pcs.end()),
      closePCs(p.close_pcs.begin(), p.close_pcs.end()),
      out(p.file, std::ios::out | std::ios::trunc),
      classes(!p.classes_file.empty())
{
    fatal_if(openPCs.empty() || closePCs.empty(),
             "%s: needs at least one open and one close address", name());
    fatal_if(!out, "%s: cannot write %s", name(), p.file);
    if (classes) {
        classesOut.open(p.classes_file, std::ios::out | std::ios::trunc);
        fatal_if(!classesOut, "%s: cannot write %s", name(),
                 p.classes_file);
    }
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
    if (counting) {
        DPRINTF(WindowInstCounter, "%#x %s: %s%s\n", op.pc,
                inst.disassemble(op.pc), enums::OpClassStrings[inst.opClass()],
                op.macro ? csprintf(" (macro-op %s: %s)",
                                    op.macro->disassemble(op.pc),
                                    enums::OpClassStrings[
                                        op.macro->opClass()])
                         : std::string());
    }
    if (inst.isMicroop() && !inst.isLastMicroop())
        return;

    // A whole instruction retired.
    const Addr pc = op.pc;
    if (counting) {
        ++count;
        if (classes) {
            fatal_if(inst.isMicroop() && !op.macro,
                     "%s: the CPU does not report the macro-op of a "
                     "micro-op, which the instruction classes need; use the "
                     "atomic CPU or no classes file", name());
            ++instClasses[(op.macro ? op.macro : op.inst)->opClass()];
        }
    }
    if (closePCs.count(pc)) {
        if (counting) {
            out << count << '\n';
            out.flush();
            if (classes)
                writeClasses();
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
    }
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

} // namespace gem5
