from m5.objects.Probe import ProbeListenerObject
from m5.params import *


class WindowInstCounter(ProbeListenerObject):
    """Counts the instructions a core retires inside windows that open and
    close at given addresses, and writes one count per window to a file and,
    with classes_file and names_file, the same instructions by OpClass and
    by name to others, with flips_file their operands' bit changes, with activity_file
    their widths, shifter uses, instruction-word bit changes, data accesses, taken
    control transfers and unit switching, and with stats_file how much the named
    statistics grew over each window (window_inst_counter.hh). Set manager to the CPU to count on."""

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
    stats = VectorParam.String(
        [], "full names of scalar statistics whose growth over each window the "
        "stats file gets (e.g. system.flash.cache.chained)"
    )
    stats_file = Param.String(
        "", "the file each window's growth of the stats is written to"
    )
    flips_file = Param.String(
        "",
        "the file each window's operand bit changes by instruction name are "
        "written to (empty: none); needs cpu",
    )
    cpu = Param.BaseCPU(
        NULL, "the CPU whose integer registers the flips file reads"
    )
    names_file = Param.String(
        "",
        "the file each window's instructions, and the micro-ops of the "
        "split ones, by name are written to (empty: none); needs the atomic "
        "CPU",
    )
    activity_file = Param.String(
        "",
        "the file each window's instruction activity (widths, shifter uses, "
        "instruction-word bit changes, data accesses by region, taken "
        "control transfers, unit switching) is written to (empty: none); "
        "Arm Thumb only, needs the atomic CPU",
    )
