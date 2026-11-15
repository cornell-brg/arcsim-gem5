from m5.params import *
from m5.SimObject import SimObject


class BaseInstLatency(SimObject):
    """What each instruction costs the atomic CPU, in place of its one
    clock period per micro-op: a per-instruction latency table for an
    instruction-level simulator whose time still means something. Set a
    subclass as an atomic CPU's inst_latency (inst_latency.hh)."""

    type = "BaseInstLatency"
    abstract = True
    cxx_header = "cpu/simple/inst_latency.hh"
    cxx_class = "gem5::BaseInstLatency"
