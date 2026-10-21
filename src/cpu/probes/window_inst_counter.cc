#include "cpu/probes/window_inst_counter.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/WindowInstCounter.hh"

namespace gem5
{

WindowInstCounter::WindowInstCounter(const WindowInstCounterParams &p)
    : ProbeListenerObject(p),
      openPCs(p.open_pcs.begin(), p.open_pcs.end()),
      closePCs(p.close_pcs.begin(), p.close_pcs.end()),
      out(p.file, std::ios::out | std::ios::trunc)
{
    fatal_if(openPCs.empty() || closePCs.empty(),
             "%s: needs at least one open and one close address", name());
    fatal_if(!out, "%s: cannot write %s", name(), p.file);
}

void
WindowInstCounter::regProbeListeners()
{
    typedef ProbeListenerArg<WindowInstCounter, Addr> Listener;
    connectListener<Listener>(this, "RetiredInstsPC",
                              &WindowInstCounter::retired);
}

void
WindowInstCounter::retired(const Addr &pc)
{
    if (counting)
        ++count;
    if (closePCs.count(pc)) {
        if (counting) {
            out << count << '\n';
            out.flush();
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
    }
}

} // namespace gem5
