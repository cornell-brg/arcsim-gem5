// The instruction activity the M33 energy model reads, counted the same way for the
// windows of WindowInstCounter and the tasks of TaskInstCounter.
//
// Activity: ten counts, with the definitions of the energy fit's m33width.py:
//   1, 2  the 16-bit and the 32-bit instructions;
//   3     those that use the shifter (thumb_traits.hh);
//   4     the bits that change from each instruction's encoding to the next one's (a
//         16-bit encoding as its halfword, a 32-bit one as both, the first high);
//   5-7   data accesses, one for each instruction or micro-op with a memory access, by
//         where its address is: reads of flash (0x10000000-0x1fffffff), reads of SRAM
//         and writes to SRAM (0x20000000-0x2fffffff), a read or a write as the whole
//         instruction's mnemonic says (thumb_traits.hh);
//   8     taken control transfers: instructions after which the next one executed is
//         not the next in memory;
//   9, 10 unit switching, from each instruction's kind (thumb_traits.hh): instructions
//         whose kind differs from the previous one's, and those whose kind none of the
//         previous four had.
// Every instruction moves the history the next one is compared with (encoding, next
// address, kinds), counted or not.
//
// Flips: the bits that change in a micro-op's integer operands against the previous
// micro-op of the same OpClass in the same operand slot (a functional unit's inputs and
// outputs from one use to the next), sources and results apart. A source is the
// register's value before the micro-op ran, from a copy of the integer registers taken
// by copyRegisters() and kept as micro-ops write them; a result is the register after
// it. Values are taken as 32 bits.

#ifndef __CPU_PROBES_INST_ACTIVITY_HH__
#define __CPU_PROBES_INST_ACTIVITY_HH__

#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>

#include "cpu/base.hh"
#include "cpu/op_class.hh"
#include "cpu/probes/thumb_traits.hh"

namespace gem5
{

class InstActivity
{
  public:
    using Counts = std::array<uint64_t, 10>;
    static const char *const countNames[10];

    /** cpu: the CPU whose registers the flips read (none: no flips). */
    explicit InstActivity(BaseCPU *cpu) : cpu(cpu) {}

    /** A whole instruction starts (its first micro-op, or itself not
     *  split) at pc: counted into c unless c is null. */
    void inst(const StaticInst &whole, Addr pc, Counts *c);

    /** A micro-op's (or an instruction's not split) data access, into c;
     *  false once 64 memory references have had no address and none has
     *  had one: the CPU reports none. */
    bool access(const BaseCPU::CommittedOp &op, const StaticInst &whole,
                Counts &c);

    /** The next instruction's address is not compared (a window opens). */
    void forgetNextPC() { nextPCValid = false; }

    /** Take the copy of the integer registers that flips() reads. */
    void copyRegisters();

    /** inst's operand bit changes, sources and results. */
    std::pair<uint64_t, uint64_t> flips(const StaticInst &inst);

  private:
    struct Encoded
    {
        uint32_t enc;
        unsigned width;
        thumb::Traits traits;
    };
    const Encoded &encoded(const StaticInst &whole);

    BaseCPU *cpu;
    uint64_t addressed = 0;  // memory references with an address
    uint64_t unaddressed = 0;
    uint32_t lastEnc = 0;
    Addr nextPC = 0;
    bool nextPCValid = false;
    std::array<thumb::Kind, 4> kinds{};  // the last ones, oldest first
    size_t numKinds = 0;
    // Machine instruction (encoding, IT state, width) -> what it is.
    std::unordered_map<uint64_t, Encoded> encodings;

    static constexpr int Slots = 4;  // operand slots kept per OpClass
    std::unordered_map<RegIndex, RegVal> copy;  // flat integer register -> value
    std::array<std::array<RegVal, Slots>, Num_OpClasses> lastSrc{};
    std::array<std::array<RegVal, Slots>, Num_OpClasses> lastDest{};
};

} // namespace gem5

#endif // __CPU_PROBES_INST_ACTIVITY_HH__
