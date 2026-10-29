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
      0x18              read: the HWPE's ext_flags (0 unless the subclass
                        gives a value); writes ignored
      0x1C              the software event register: reads 0, writes
                        ignored
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
    mux_channels = Param.Unsigned(
        "Channels of the streamer's TCDM mux (the RTL's NB_IN_CHAN, a power "
        "of two); stream n is channel n"
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
    mux_channels = 1


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
    # vvaddStreamer: a, b and c on channels 0 to 2 of 4
    mux_channels = 4
    # Calibrated against rtl-sim (hwpe-vvadd, -wide-1 to -wide-8) with
    # HCITcdm: 6 cycles from the TRIGGER write to the first TCDM grant,
    # and 4 from the last write's grant to STATUS reading 0 (the write's
    # acknowledgement takes 1 of them, done_latency the others).
    start_latency = 6
    done_latency = 2
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


class HWPEFastCellBestK(HWPEBase):
    """
    arcsim-xcel's FAST corner detector on one cell,
    fast-with-scoring-wide-multicycle-cell-best-k
    (fastTopWithScoringWideMulticycleCellBestK): a job runs FAST-n on one
    cell_width x cell_height cell of a frame, and writes the cell's best k
    corners after non-max suppression. The defaults are those of
    arcsim-soc's hwpe_xcel wrapper.

    Job registers: 0x40 IMAGE_PTR, the cell's top-left pixel (8-bit
    pixels, row-major, rows frame_width bytes apart), and 0x44 CORNERS_PTR,
    where the corner words go. 0x18 reads the corners the cell has (before
    non-max suppression); a job's start and every soft clear reset it.

    The engine scores each pixel of the cell without its 3-pixel border:
    the largest d for which n contiguous pixels of the 16-pixel ring are
    all more than d brighter, or all more than d darker, than the centre.
    A pixel with a score of at least threshold is a corner. A corner is
    kept when its score is greater than that of each of its 8 neighbours
    (0 outside the scored area). The k kept corners with the highest
    scores (on a tie, the first in raster order) are written as one beat
    to CORNERS_PTR, best first, one word each: {score, row, col}, with row
    and col in the cell, each log2 of the cell's height or width bits
    wide. The beat's other words are not written (their byte enables are
    off); without a corner, the beat is written with no byte enabled.

    Timing: the cell is read as a 2D walk of beats of 4 * words bytes (one
    pixel a byte), cell_width / (4 * words) beats a row. The engine takes
    one pixel at a time: a pixel that completes a 7 x 7 window (row and col
    at least 6) occupies it for cycles + 4 cycles, for the scoring
    (fastScoreMulticycle); any other pixel for 2. After the last pixel,
    the scoring, the non-max suppression and the best-k list take
    cycles + 4 + (cell_width - 6) + 3 cycles before the corner beat goes to
    the sink. So the job's time does not depend on the pixels.
    """

    type = "HWPEFastCellBestK"
    cxx_class = "gem5::HWPEFastCellBestK"
    cxx_header = "dev/hwpe/hwpe_fast_cell_best_k.hh"

    num_io_regs = 2
    # fastStreamer: pixels and corners on channels 0 and 1
    mux_channels = 2
    # vvadd's values (the same control slave and streamer), not yet
    # calibrated for this HWPE
    start_latency = 6
    done_latency = 3
    cell_width = Param.Unsigned(64, "Pixels in a row of the cell")
    cell_height = Param.Unsigned(64, "Rows of the cell")
    frame_width = Param.Unsigned(
        320, "Bytes from one row of the frame to the next"
    )
    n = Param.Unsigned(9, "Contiguous ring pixels a corner needs (FAST-n)")
    threshold = Param.Unsigned(10, "Lowest score of a corner (at least 1)")
    cycles = Param.Unsigned(
        4, "Cycles of fastScoreMulticycle's arc stage: 1, 2, 4, 8 or 16"
    )
    k = Param.Unsigned(3, "Best corners written (1 to words)")
    words = Param.Unsigned(
        8, "32-bit words per TCDM access (the RTL's (MEM_DW - 32) / 32)"
    )
    buffer_depth = Param.Unsigned(
        2, "Pixel beats buffered (the engine's pixel FIFO depth)"
    )


class HWPESelect(BasicPioDevice):
    """
    The optical-flow wrapper's SELECT register (hwpe-optical-flow.v, at its
    window's 0x3000): bits 1:0 read back what was written; the engines
    share the TCDM port through HCIMux whatever it holds. Every other
    offset reads 0 and ignores writes.
    """

    type = "HWPESelect"
    cxx_class = "gem5::HWPESelect"
    cxx_header = "dev/hwpe/hwpe_select.hh"

    pio_addr = 0x50803000
    pio_size = Param.Addr(0x1000, "Size of the register window")
