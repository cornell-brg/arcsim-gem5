// Counts the instructions a core retires inside measurement windows that the program
// marks by address: a window opens when an instruction at an open address retires and
// closes when one at a close address retires. The count of a window is the instructions
// retired after the opening one, up to and including the closing one. Each closed window
// adds one line, its count, to a file, in the order the windows ran.
//
// With a classes file, each window also adds a line there with the same instructions by
// gem5's OpClass, two ways, as name=count pairs for the classes that occur:
// inst_<class> counts whole instructions by the class of the instruction itself (for an
// instruction split into micro-ops, its macro-op's class), and op_<class> counts
// micro-ops by their own class (an instruction that is not split is one micro-op). The
// inst_ counts add up to the window's count.
//
// With a names file, each window adds a line there with the same instructions by name
// (gem5's mnemonic), as name=count pairs sorted by name: i.<name> for whole
// instructions (the macro-op's name for split ones), which add up to the window's
// count, and o.<name> for the micro-ops of the split ones only (an instruction that is
// not split is its own one micro-op, already in i.).
//
// With a flips file (and the CPU, to read its registers), each window adds a line there
// with the bits that change in integer operands, by the whole instruction's name:
// s.<name> for its source operands and d.<name> for its results, against the previous
// micro-op of the same OpClass in the same operand slot (a functional unit's inputs and
// outputs from one use to the next). A source is the register's value before the
// micro-op ran, from a copy of the integer registers taken when the window opens and
// kept as micro-ops write them; a result is the register after it. Values are taken as
// 32 bits (both cores are 32-bit). Floating-point and condition registers are not counted.
//
// With an activity file (Arm Thumb cores, atomic CPU), each window adds a line there of
// ten counts, separated by spaces, with the definitions of the energy fit's m33width.py:
//   1, 2  the 16-bit and the 32-bit instructions;
//   3     those that use the shifter (thumb_traits.hh);
//   4     the bits that change from each instruction's encoding to the next one's (a
//         16-bit encoding as its halfword, a 32-bit one as both, the first high),
//         counted from the instruction before the window's first;
//   5-7   data accesses, one for each instruction or micro-op with a memory access
//         (as the execution trace's ExecEffAddr shows them), by where its address is:
//         reads of flash (0x10000000-0x1fffffff), reads of SRAM and writes to SRAM
//         (0x20000000-0x2fffffff), a read or a write as the whole instruction's
//         mnemonic says (thumb_traits.hh);
//   8     taken control transfers: instructions after which the next one executed is
//         not the next in memory (the window's first is not counted);
//   9, 10 unit switching, from each instruction's kind (thumb_traits.hh): instructions
//         whose kind differs from the previous one's, and those whose kind none of the
//         previous four had, the previous ones counted across windows.
// A split instruction counts when its first micro-op commits, as the execution trace
// shows its macro-op then, so the micro-ops of one that closes a window are outside it
// and those of one that opens a window inside it.
//
// With a stats file and the full names of scalar statistics (e.g.
// system.flash.cache.chained), each window adds a line there with how much each grew
// over the window, as name=value pairs in the order given. Any CPU model; the
// statistics change when their objects act, which may be a few cycles off the
// window's committed edges.
//
// It listens to the CPU's CommittedOps probe point, which the CPU models notify for
// every committed instruction and micro-op; a whole instruction ends at its last
// micro-op, as for the RetiredInstsPC probe point. Only the atomic CPU reports the
// macro-op of a micro-op, so the classes and names need it.

#ifndef __CPU_PROBES_WINDOW_INST_COUNTER_HH__
#define __CPU_PROBES_WINDOW_INST_COUNTER_HH__

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/stats/info.hh"
#include "cpu/base.hh"
#include "cpu/op_class.hh"
#include "cpu/probes/thumb_traits.hh"
#include "params/WindowInstCounter.hh"
#include "sim/probe/probe_listener_object.hh"

namespace gem5
{

class WindowInstCounter : public ProbeListenerObject
{
  public:
    WindowInstCounter(const WindowInstCounterParams &params);

    void regProbeListeners() override;
    void startup() override;

    void committed(const BaseCPU::CommittedOp &op);

  private:
    void writeClasses();
    void writeNames();
    void writeFlips();
    void writeStats();
    void writeActivity();
    void countFlips(const StaticInst &inst, const StaticInst &whole);
    void copyRegisters();

    struct Encoded
    {
        uint32_t enc;
        unsigned width;
        thumb::Traits traits;
    };
    const Encoded &encoded(const StaticInst &whole);
    void activityInst(const StaticInst &whole, Addr pc);
    void activityAccess(const BaseCPU::CommittedOp &op,
                        const StaticInst &whole);

    std::unordered_set<Addr> openPCs;
    std::unordered_set<Addr> closePCs;
    std::ofstream out;
    std::ofstream classesOut;
    std::ofstream namesOut;
    std::ofstream flipsOut;
    std::ofstream statsOut;
    std::ofstream activityOut;
    const std::vector<std::string> statNames;
    std::vector<const statistics::ScalarInfo *> statInfos;
    std::vector<double> statStart;
    const bool classes;
    const bool names;
    const bool flips;
    const bool activity;
    BaseCPU *cpu;
    bool counting = false;
    uint64_t count = 0;
    std::array<uint64_t, Num_OpClasses> instClasses{};
    std::array<uint64_t, Num_OpClasses> opClasses{};
    std::map<std::string, uint64_t> instNames;
    std::map<std::string, uint64_t> opNames;

    static constexpr int Slots = 4;  // operand slots kept per OpClass
    std::unordered_map<RegIndex, RegVal> copy;  // flat integer register -> value
    std::array<std::array<RegVal, Slots>, Num_OpClasses> lastSrc{};
    std::array<std::array<RegVal, Slots>, Num_OpClasses> lastDest{};
    std::map<std::string, std::pair<uint64_t, uint64_t>> nameFlips;  // s, d

    // The activity file's window opens and closes at a split instruction's
    // first micro-op, so it keeps its own.
    bool activityOpen = false;
    std::array<uint64_t, 10> activityCounts{};
    uint32_t lastEnc = 0;
    Addr nextPC = 0;
    bool nextPCValid = false;
    std::array<thumb::Kind, 4> kinds{};  // the last ones, oldest first
    size_t numKinds = 0;
    // Machine instruction (encoding, IT state, width) -> what it is.
    std::unordered_map<uint64_t, Encoded> encodings;
};

} // namespace gem5

#endif // __CPU_PROBES_WINDOW_INST_COUNTER_HH__
