from m5.objects.Probe import ProbeListenerObject
from m5.params import *


class TaskInstCounter(ProbeListenerObject):
    """Counts the instructions and cycles of each task a program runs (one
    run of a node's step), by the marks the program makes: a call to its
    mark function (mark_pc) with a kind and a value in two registers.
    Writes one line a task, and at the end one a context outside the
    tasks (task_inst_counter.hh). Set manager and cpu to the CPU to count
    on; any CPU model."""

    type = "TaskInstCounter"
    cxx_header = "cpu/probes/task_inst_counter.hh"
    cxx_class = "gem5::TaskInstCounter"

    cpu = Param.BaseCPU("the CPU whose registers and cycles it reads")
    mark_pc = Param.Addr("the address of the program's mark function")
    arg_regs = VectorParam.Unsigned(
        [0, 1],
        "the integer registers that hold a mark's kind and value (Arm: r0 "
        "and r1; RISC-V: a0 and a1, 10 and 11)",
    )
    file = Param.String("the file the lines are written to")
    classifier = Param.BaseInstLatency(
        NULL,
        "what classes each instruction (classOf) and tells exceptions "
        "(activeException); it need not be the CPU's own inst_latency. "
        "Without one, no class counts and no exceptions",
    )
    stats = VectorParam.String(
        [],
        "full names of scalar statistics whose growth each line gets (e.g. "
        "system.flash.cache.misses)",
    )
