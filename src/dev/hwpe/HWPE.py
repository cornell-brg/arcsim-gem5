from m5.objects.Device import BasicPioDevice
from m5.params import *


class HWPEBase(BasicPioDevice):
    """
    A Hardware Processing Engine (HWPE): an accelerator a core offloads jobs
    to. The shell every HWPE shares (arcsim-soc's hwpe_xcel: a periph port
    for the registers and a TCDM port for the data) and its standard control
    (hwpe_ctrl_slave): an HWPE subclass gives only the job.

    Registers, each a 32-bit word also read and written by byte and halfword
    (a narrow write changes only its bytes):
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

    A job starts start_latency cycles after its trigger (or after the
    previous job's done), runs until the subclass reports it done, and is
    done done_latency cycles later: STATUS and FINISHED show it and irq
    pulses.

    Differs from arcsim-soc's prototype (hwpe-vvadd), which follows the
    hwpe_ctrl RTL more literally: there the window repeats every 0x400,
    0x100 and 0x200 read a given context's job registers, job registers
    past the last alias other contexts' and the generic ones, TRIGGER reads
    0xdeadbeef, FINISHED saturates at 2 and also clears on a write, and only
    a write of 0 to SOFT_CLEAR resets STATUS and the job ids.
    """

    type = "HWPEBase"
    cxx_class = "gem5::HWPEBase"
    cxx_header = "dev/hwpe/hwpe_base.hh"
    abstract = True

    pio_addr = 0x50800000
    pio_size = Param.Addr(0x100000, "Size of the register window (1 MiB)")
    tcdm_port = RequestPort("The HWPE's port to the TCDM")
    # A one-cycle pulse, as the HWPE's evt_o: in a PULP cluster the event
    # unit latches it. A RISC-V interrupt input usually expects a level held
    # until software clears its cause, so connecting it to one may need a
    # latch in between.
    irq = IntSourcePin("Job done event, a one-cycle pulse; may stay unconnected")

    num_contexts = Param.Unsigned(2, "Jobs that can be queued (1 to 4)")
    num_generic_regs = Param.Unsigned(8, "Generic registers from 0x20 (at most 8)")
    num_io_regs = Param.Unsigned(
        "Job registers from 0x40, one set per context (at most 48)"
    )
    start_latency = Param.Cycles(
        1,
        "From a job's trigger, or the previous job's done, to its start",
    )
    done_latency = Param.Cycles(
        1, "From the job's end to STATUS and FINISHED showing it done"
    )


class HWPEEmpty(HWPEBase):
    """
    An HWPE without an engine: the standard registers, and a job that makes
    no accesses and ends as it starts.
    """

    type = "HWPEEmpty"
    cxx_class = "gem5::HWPEEmpty"
    cxx_header = "dev/hwpe/hwpe_empty.hh"

    num_io_regs = 4
