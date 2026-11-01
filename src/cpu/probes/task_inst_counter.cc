#include "cpu/probes/task_inst_counter.hh"

#include <cmath>

#include "arch/generic/isa.hh"
#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/TaskInstCounter.hh"
#include "sim/cur_tick.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

TaskInstCounter::TaskInstCounter(const TaskInstCounterParams &p)
    : ProbeListenerObject(p),
      cpu(p.cpu),
      markPC(p.mark_pc & ~Addr(1)),
      out(p.file, std::ios::out | std::ios::trunc),
      classifier(p.classifier),
      statNames(p.stats)
{
    fatal_if(!cpu, "%s: needs the cpu", name());
    fatal_if(!out, "%s: cannot write %s", name(), p.file);
    fatal_if(p.arg_regs.size() != 2,
             "%s: arg_regs names the kind's and the value's registers",
             name());
    for (unsigned r : p.arg_regs)
        argRegs.push_back(r);
    // One column a class name, in the classes' order, then c.other.
    std::map<std::string, size_t> named;
    if (classifier) {
        for (const std::string &n : classifier->classNames()) {
            auto it = named.find(n);
            if (it == named.end()) {
                it = named.emplace(n, columns.size()).first;
                columns.push_back(n);
            }
            classColumn.push_back(it->second);
        }
    }
    columns.push_back("other");
    registerExitCallback([this]() { finish(); });
}

void
TaskInstCounter::startup()
{
    for (const std::string &n : statNames) {
        const auto *info = dynamic_cast<const statistics::ScalarInfo *>(
            statistics::resolve(n));
        fatal_if(!info, "%s: no scalar statistic named %s", name(), n);
        statInfos.push_back(info);
        statLast.push_back(info->result());
    }
}

void
TaskInstCounter::regProbeListeners()
{
    connectListener<ProbeListenerArg<TaskInstCounter, BaseCPU::CommittedOp>>(
        this, "CommittedOps", &TaskInstCounter::committed);
    connectListener<ProbeListenerArg<TaskInstCounter, bool>>(
        this, "Sleeping", &TaskInstCounter::sleeping);
}

TaskInstCounter::Counts &
TaskInstCounter::other(const std::string &name)
{
    auto it = others.find(name);
    if (it == others.end()) {
        Counts c;
        c.classes.assign(columns.size(), 0);
        c.stats.assign(statInfos.size(), 0.0);
        it = others.emplace(name, std::move(c)).first;
    }
    return it->second;
}

TaskInstCounter::Counts &
TaskInstCounter::context()
{
    if (classifier) {
        const int e = classifier->activeException(cpu->getContext(0));
        if (e)
            return other(csprintf("exception.%d", e));
    }
    if (thread == Before)
        return other("before");
    if (thread == NoNode)
        return other("none");
    auto it = open.find(thread);
    if (it != open.end())
        return it->second.counts;
    return other(csprintf("node.%d", thread));
}

void
TaskInstCounter::readStats(Counts &c)
{
    for (size_t i = 0; i < statInfos.size(); ++i) {
        const double v = statInfos[i]->result();
        c.stats[i] += v - statLast[i];
        statLast[i] = v;
    }
}

void
TaskInstCounter::committed(const BaseCPU::CommittedOp &op)
{
    const StaticInst &inst = *op.inst;
    if (inst.isMemRef() && memClass == No_OpClass)
        memClass = inst.opClass();
    if (inst.isMicroop() && !inst.isLastMicroop())
        return;

    // A whole instruction retired.
    Counts &c = context();
    if (&c != last) {
        // The context changed: the one before takes the statistics' growth
        // so far.
        if (last)
            readStats(*last);
        last = &c;
    }
    const Cycles now = cpu->curCycle();
    if (!started) {
        started = true;
        lastCycle = now;
    }
    c.cycles += now - lastCycle;
    lastCycle = now;
    ++c.insts;
    if (classifier) {
        fatal_if(inst.isMicroop() && !op.macro,
                 "%s: the CPU does not report the macro-op of a micro-op, "
                 "which the classes need; use the atomic or Minor CPU",
                 name());
        const StaticInst &whole = op.macro ? *op.macro : inst;
        const int ci = classifier->classOf(whole, memClass);
        ++c.classes[ci >= 0 ? classColumn[ci] : columns.size() - 1];
    }
    memClass = No_OpClass;

    if ((op.pc & ~Addr(1)) == markPC) {
        ThreadContext *tc = cpu->getContext(0);
        const RegClass &ints =
            *tc->getIsaPtr()->regClasses().at(IntRegClass);
        mark(tc->getReg(ints[argRegs[0]]), tc->getReg(ints[argRegs[1]]));
    }
}

void
TaskInstCounter::sleeping(const bool &now_asleep)
{
    if (now_asleep == asleep)
        return;
    asleep = now_asleep;
    const Cycles now = cpu->curCycle();
    if (asleep) {
        if (last) {
            last->cycles += started ? now - lastCycle : 0;
            readStats(*last);
        }
        sleepStart = now;
        last = &other("sleep");
    } else {
        Counts &s = other("sleep");
        s.cycles += now - sleepStart;
        readStats(s);
        last = nullptr;
    }
    started = true;
    lastCycle = now;
}

void
TaskInstCounter::mark(uint32_t kind, uint32_t value)
{
    DPRINTF(TaskInstCounter, "mark %u %#x\n", kind, value);
    switch (kind) {
      case 1:
        thread = value == 0xffffffffu ? NoNode : int64_t(value);
        break;
      case 2: {
        thread = value;
        auto it = open.find(value);
        if (it != open.end()) {
            warn("%s: node %u's task %llu begins again before it ended",
                 name(), value, it->second.index);
            if (last == &it->second.counts)
                last = &other(csprintf("node.%d", value));
            open.erase(it);
        }
        Task t;
        t.index = taskCount[value]++;
        t.begin = curTick();
        t.counts.classes.assign(columns.size(), 0);
        t.counts.stats.assign(statInfos.size(), 0.0);
        open.emplace(value, std::move(t));
        break;
      }
      case 3: {
        auto it = open.find(value);
        if (it == open.end()) {
            warn("%s: node %u's task ends but none began", name(), value);
            break;
        }
        Task &t = it->second;
        if (last == &t.counts) {
            readStats(t.counts);
            last = nullptr;
        }
        write(csprintf("task node=%u index=%llu begin=%llu end=%llu", value,
                       t.index, t.begin, curTick()),
              t.counts);
        open.erase(it);
        break;
      }
      default:
        warn_once("%s: a mark of unknown kind %u", name(), kind);
    }
}

void
TaskInstCounter::write(const std::string &head, const Counts &c)
{
    out << head << " insts=" << c.insts << " cycles=" << c.cycles;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (c.classes[i])
            out << " c." << columns[i] << '=' << c.classes[i];
    }
    for (size_t i = 0; i < statNames.size(); ++i) {
        const double v = c.stats[i];
        out << " s." << statNames[i] << '=';
        if (v == std::floor(v))
            out << static_cast<long long>(v);
        else
            out << v;
    }
    out << '\n';
}

void
TaskInstCounter::finish()
{
    if (last)
        readStats(*last);
    last = nullptr;
    for (const auto &[node, t] : open) {
        write(csprintf("open node=%u index=%llu begin=%llu", node, t.index,
                       t.begin),
              t.counts);
    }
    for (const auto &[name, c] : others)
        write("other context=" + name, c);
    out.flush();
}

} // namespace gem5
