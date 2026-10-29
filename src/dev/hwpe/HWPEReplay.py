from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class HWPEReplay(ClockedObject):
    """
    Plays the jobs of an RTL testbench's HWPE trace (arcsim-xcel's
    tb-utils/hwpeTraceMonitor.sv) into an HWPE model: each register write
    (and read of ACQUIRE or FINISHED) at the cycle after the last done
    event that the RTL made it, and before each job the words the RTL
    engine read in it, as the RTL read them, loaded into memory. The
    model's own trace (debug flag HWPETrace) can then be compared with the
    RTL's. Exits when the model has signalled as many done events as the
    trace holds.
    """

    type = "HWPEReplay"
    cxx_class = "gem5::HWPEReplay"
    cxx_header = "dev/hwpe/hwpe_replay.hh"

    trace = Param.String("The RTL trace")
    hwpe_base = Param.Addr("The HWPE's register window")
    mem_base = Param.Addr(0, "Added to the trace's TCDM addresses")
    hwpe_port = RequestPort("To the HWPE's registers (pio)")
    mem_port = RequestPort("To the TCDM, for loading it")
    irq = IntSinkPin("The HWPE's done event")
