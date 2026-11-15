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

    In atomic mode (an atomic CPU) a job's TCDM accesses are atomic and take
    no time: the job starts atomic_latency cycles after its TRIGGER (or
    after the previous job is done), makes all its accesses at that tick and
    counts as done at once. So its results appear when it counts as done.
    With `timed_in_atomic`, a job keeps the timing it has with a timing CPU.

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
    atomic_latency = Param.Cycles(
        5,
        "In atomic mode, cycles from a job's TRIGGER (or the previous job "
        "being done) to its start, at which it is also done; in place of "
        "start_latency, done_latency and the job's own timing",
    )
    timed_in_atomic = Param.Bool(
        False,
        "With an atomic CPU, a job still takes the cycles it takes with a "
        "timing CPU, its TCDM accesses made as timing accesses",
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
    write to c is acknowledged. In atomic mode (see HWPEBase) the job takes
    no time: compute_latency and compute_interval are not used.
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
    # Not calibrated
    atomic_latency = 5
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



class HWPEFastCell(HWPEBase):
    """
    arcsim-xcel's FAST cell accelerator,
    fast-with-scoring-wide-multicycle-cell-best-k (top
    fastTopWithScoringWideMulticycleCellBestK): FAST-9 with threshold 10
    on one 64 x 64 cell of a 320-wide frame per job; the cell's corner
    count, and its best K = 3 corners after non-max suppression, written
    as one word each, {12'b0, score, row, col}, best first, at CORNERS_PTR
    (only as many words as there are; none without corners).

    Job registers: 0x40 IMAGE_PTR (the cell's top-left pixel, 32-byte
    aligned), 0x44 CORNERS_PTR. 0x18 (ext_flags): COUNT, the last finished
    job's corner count; a soft clear sets it to 0.

    Structure, as the RTL's blocks. The top's job FSM starts the source
    once per row y = 3 .. 60 of corners, for the band rows y-4 .. y+4 in
    the cell (8 at y = 3 and 60, else 9): d0 the band rows (320 bytes
    apart), d1 the 32 / chunk chunks of a word (the same word read
    again), d2 the row's two 32-byte words. The source (stream pix) reads
    only in cycles in which the engine is ready, as hci_core_source's req
    is gated by stream.ready; it holds one beat (pix_depth 2: that beat
    and a read in flight). The engine (FastCellEngine) loads one word per
    band row and chunk while ready, then steps a column a cycle through
    the chunk: three fastScore units give rows y-1, y, y+1, a 3x3 window
    of scores gives non-max suppression, a passing corner goes into the
    best 3; 64 steps and 3 flush steps a row. Then the sink (stream
    corner) writes the best corner words in one masked access. The engine
    is evaluated once per clock edge, as the RTL's always_ff.

    The streamer's mux has two channels (fastCellStreamer): pix on 0,
    corner on 1. In atomic mode (see HWPEBase) the job takes no time.
    """

    type = "HWPEFastCell"
    cxx_class = "gem5::HWPEFastCell"
    cxx_header = "dev/hwpe/hwpe_fast_cell.hh"

    num_io_regs = 2
    mux_channels = 2
    chunk = Param.Unsigned(
        8, "Columns loaded at once (the RTL's CHUNK; a power of two to 32)"
    )
    threshold = Param.Unsigned(10, "FAST threshold (fastCellPackage)")
    pix_depth = Param.Unsigned(
        2,
        "Beats of the pixel source: hci_core_source's held beat and one "
        "read in flight",
    )
    source_start_latency = Param.Cycles(
        2,
        "Cycles from the source's req_start to its first read "
        "(hci_core_source's address generator and address FIFO)",
    )
    row_restart_latency = Param.Cycles(
        2,
        "Cycles from the engine taking a row's last beat to the source's "
        "req_start for the next row (the source's done, then the top's "
        "src_busy and src_y registers)",
    )
    sink_start_latency = Param.Cycles(
        2,
        "Cycles from the engine entering OUT (the sink's req_start) to the "
        "sink's write request",
    )
    sink_done_latency = Param.Cycles(
        3,
        "Cycles from the write's grant to the top seeing the sink done "
        "(hci_core_sink's registered done, the top's sink_done)",
    )
    # From rtl-sim: the TRIGGER write's handshake to the top's WORKING is
    # 5 cycles (hwpe_ctrl_slave's start, the top's STARTING); the slave
    # clears STATUS at the edge after the top's FINISHED, so a read in the
    # next cycle sees it (done_latency 1)
    start_latency = 5
    done_latency = 1
    # IL: a job's latency in atomic mode, the RTL's job from the TRIGGER
    # write to the top's FINISHED: 8117 cycles with corners, 8112 without
    # (no write), whatever the cell. The core then reads 8127 (the atomic
    # CPU's polling loop rounds up to its next STATUS read); rtl-sim's core
    # reads 8129 and 8124.
    atomic_latency = 8117
