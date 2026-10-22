from m5.objects.Probe import ProbeListenerObject
from m5.params import *


class WindowInstCounter(ProbeListenerObject):
    """Counts the instructions a core retires inside windows that open and
    close at given addresses, and writes one count per window to a file and,
    with classes_file and names_file, the same instructions by OpClass and
    by name to others (window_inst_counter.hh). Set manager to the CPU to count on."""

    type = "WindowInstCounter"
    cxx_header = "cpu/probes/window_inst_counter.hh"
    cxx_class = "gem5::WindowInstCounter"

    open_pcs = VectorParam.Addr("addresses whose instruction opens a window")
    close_pcs = VectorParam.Addr("addresses whose instruction closes a window")
    file = Param.String("the file each window's count is written to")
    classes_file = Param.String(
        "",
        "the file each window's instructions and micro-ops by OpClass are "
        "written to (empty: none); needs the atomic CPU",
    )
    names_file = Param.String(
        "",
        "the file each window's instructions, and the micro-ops of the "
        "split ones, by name are written to (empty: none); needs the atomic "
        "CPU",
    )
