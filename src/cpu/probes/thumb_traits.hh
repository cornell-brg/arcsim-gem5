// What WindowInstCounter's activity file needs to know about a Thumb (Arm M-profile)
// instruction from its encoding alone: its unit kind, whether it uses the shifter, and
// whether its memory accesses are writes. The definitions are those of the energy fit's
// m33width.py, which reads them from GNU objdump's disassembly, so this names each
// encoding as objdump does and applies the same rules to the name:
//
// - kind: one of the kinds below, from the mnemonic, with the flag-setting s and a
//   condition code stripped as needed (KindTable in thumb_traits.cc); fp for VFP, other
//   for a mnemonic not in the table.
// - shift: the disassembly names a shift: the shifts themselves (lsls, ror.w ...) and
//   operands shifted on the way in (eor.w r0, r1, r2, ror #8; ldr r0, [r1, r2, lsl #2]).
//   Inside an IT block objdump writes the condition after the mnemonic (lslle), which
//   then no longer reads as a shift.
// - write: the mnemonic starts with str, stm, push, vstr, vstm or vpush.

#ifndef __CPU_PROBES_THUMB_TRAITS_HH__
#define __CPU_PROBES_THUMB_TRAITS_HH__

#include <cstdint>
#include <string>

namespace gem5
{
namespace thumb
{

enum class Kind : uint8_t
{
    Alu, Bit, Mul, Div, Load, Store, Multi, Branch, Jump, Fp, Other
};

struct Traits
{
    Kind kind;
    bool shift;
    bool write;
};

// enc is a 16-bit instruction's halfword, or a 32-bit one's two halfwords, the first
// high; width is 2 or 4 bytes. itCond is the condition (0-14) an enclosing IT block
// gives the instruction, or -1 outside one.
Traits traits(uint32_t enc, unsigned width, int itCond);

// The mnemonic objdump would print, without its suffixes (.w, .f32 ...) but with the
// condition code of a conditional branch or of an IT block.
std::string mnemonic(uint32_t enc, unsigned width, int itCond);

} // namespace thumb
} // namespace gem5

#endif // __CPU_PROBES_THUMB_TRAITS_HH__
