from m5.objects.Device import BasicPioDevice
from m5.params import *


class HWPEBase(BasicPioDevice):
    """
    A Hardware Processing Engine (HWPE): an accelerator that a core
    offloads jobs to. This base class models what all HWPEs share
    (arcsim-soc's hwpe_xcel): a peripheral port (pio) for the registers, a
    TCDM port for the data, and the standard control logic
    (hwpe_ctrl_slave). A subclass adds only the computation.

    The registers are 32-bit words. They can also be accessed by byte or
    halfword; a narrow write changes only the bytes it writes.
      0x00 TRIGGER      write: queue the job in the context being programmed
      0x04 ACQUIRE      read: reserve a context and return the job's id;
                        -1 while every context holds a job, -2 while an
                        acquired context waits for its trigger
      0x08 FINISHED     jobs done since the last read; a read clears it
      0x0C STATUS       byte n is 1 while context n's job is queued or running
      0x10 RUNNING_JOB  the running job's id (the jobs done since the clear)
      0x14 SOFT_CLEAR   write: drop every job and reset the job state; a write
                        of 0 also clears the generic and job registers
      0x18, 0x1C        the reserved and software event registers: read 0,
                        writes ignored
      0x20 + 4n         generic register n, shared by every job
      0x40 + 4n         job register n of the context being programmed
    Everything else in the window reads 0 and ignores writes, with a warning
    the first time.

    Timing of a job: it starts start_latency cycles after its TRIGGER
    (or, if another job is running, after that job is done). It runs until
    the subclass reports that its last access has completed. done_latency
    cycles later the job counts as done: STATUS and FINISHED show it, and
    irq pulses.

    Differences from arcsim-soc's prototype (hwpe-vvadd), which follows the
    hwpe_ctrl RTL more literally. In the prototype:
      - the register window repeats every 0x400;
      - 0x100 and 0x200 read the job registers of a fixed context;
      - job registers past the last one alias other contexts' job
        registers and the generic registers;
      - TRIGGER reads 0xdeadbeef;
      - FINISHED saturates at 2 and is also cleared by a write;
      - only a write of 0 to SOFT_CLEAR resets STATUS and the job ids.
    """

    type = "HWPEBase"
    cxx_class = "gem5::HWPEBase"
    cxx_header = "dev/hwpe/hwpe_base.hh"
    abstract = True

    pio_addr = 0x50800000
    pio_size = Param.Addr(0x100000, "Size of the register window (1 MiB)")
    tcdm_port = RequestPort("The HWPE's port to the TCDM")
    # A one-cycle pulse, like the HWPE's evt_o. In a PULP cluster the event
    # unit latches it. A RISC-V interrupt input usually expects a level that
    # stays high until software clears its cause, so connecting irq to one
    # may need a latch in between.
    irq = IntSourcePin("Job done event, a one-cycle pulse; may stay unconnected")

    num_contexts = Param.Unsigned(2, "Jobs that can be queued (1 to 4)")
    num_generic_regs = Param.Unsigned(8, "Generic registers from 0x20 (at most 8)")
    num_io_regs = Param.Unsigned(
        "Job registers from 0x40, one set per context (at most 48)"
    )
    start_latency = Param.Cycles(
        1,
        "Cycles from a job's TRIGGER (or the previous job being done) to "
        "its start",
    )
    done_latency = Param.Cycles(
        1,
        "Cycles from the job's last completed access to STATUS and "
        "FINISHED showing it done",
    )


class HWPEEmpty(HWPEBase):
    """
    An HWPE without computation: it has the standard registers, and each
    job ends as soon as it starts, without any TCDM access.
    """

    type = "HWPEEmpty"
    cxx_class = "gem5::HWPEEmpty"
    cxx_header = "dev/hwpe/hwpe_empty.hh"

    num_io_regs = 4


class HWPEVvaddWide(HWPEBase):
    """
    arcsim-xcel's vector-vector add, hwpe-vvadd-wide (with elems = 1 it is
    hwpe-vvadd): C[i] = A[i] + B[i] for LEN 32-bit elements, wrapping on
    overflow.

    Job registers: 0x40 A_PTR, 0x44 B_PTR, 0x48 C_PTR (byte addresses) and
    0x4C LEN (number of elements).

    Each TCDM access of sources a and b reads elems elements, and each
    access of sink c writes elems elements. So a job makes
    ceil(LEN / elems) accesses per stream; the last one moves only the
    elements that are left. The engine takes one beat from each of a and b
    and adds them. The sum is ready for c compute_latency cycles later, and
    the next pair can be taken compute_interval cycles later. The RTL's
    adds are combinational, which gives 0 and 1. The job ends when the last
    write to c is acknowledged.
    """

    type = "HWPEVvaddWide"
    cxx_class = "gem5::HWPEVvaddWide"
    cxx_header = "dev/hwpe/hwpe_vvadd_wide.hh"

    num_io_regs = 4
    # Calibrated against rtl-sim (hwpe-vvadd, -wide-2, -wide-8) with
    # HCITcdm: 6 cycles from the TRIGGER write to the first TCDM grant.
    # From the last write's grant to STATUS reading 0, rtl-sim takes 4
    # cycles and gem5 5 (the write's acknowledgement takes a cycle and
    # comes within the next). But for most jobs the streamer's round robin
    # ends one cycle before the RTL's (hci_core_mux_dynamic orders the
    # accesses differently), so 3 matches most jobs exactly and the others
    # within one cycle.
    start_latency = 6
    done_latency = 3
    elems = Param.Unsigned(8, "Elements per TCDM access (1 to 8)")
    buffer_depth = Param.Unsigned(
        2, "Beats buffered in each of a, b and c (the RTL's FIFO depth)"
    )
    compute_latency = Param.Cycles(
        0, "Cycles from taking a beat of a and b to its sum being ready for c"
    )
    compute_interval = Param.Cycles(
        1, "Minimum cycles between taking two pairs of beats from a and b"
    )
