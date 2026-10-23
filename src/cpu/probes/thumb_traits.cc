#include "cpu/probes/thumb_traits.hh"

#include <string_view>
#include <unordered_map>

namespace gem5
{
namespace thumb
{

namespace
{

// The unit kind of each mnemonic (m33width.py's KIND).
const std::unordered_map<std::string_view, Kind> KindTable = [] {
    std::unordered_map<std::string_view, Kind> t;
    const std::pair<Kind, const char *> lists[] = {
        {Kind::Alu, "adc add addw adr and asr bic cmn cmp eor lsl lsr mov movt "
                    "movw mvn neg orn orr ror rrx rsb sbc sub subw teq tst uxtab "
                    "uxtah uxtb uxth sxtab sxtb sxth nop sel qadd qsub sadd16 "
                    "uqadd8 uqsub8 usub8 ssat usat it ite itt itee itet "
                    "itte ittt iteee iteet itete itett ittee ittet ittte itttt"},
        {Kind::Bit, "bfc bfi clz rbit rev rev16 revsh sbfx ubfx"},
        {Kind::Mul, "mla mls mul smlabb smlal smulbb smull umlal umull smlad "
                    "smuad"},
        {Kind::Div, "sdiv udiv"},
        {Kind::Load, "ldr ldrb ldrd ldrh ldrsb ldrsh ldrex"},
        {Kind::Store, "str strb strd strh strex"},
        {Kind::Multi, "ldm ldmia ldmdb ldmfd stm stmia stmdb stmea push pop"},
        {Kind::Branch, "b cbz cbnz tbb tbh"},
        {Kind::Jump, "bl blx bx"},
    };
    for (const auto &[kind, names] : lists) {
        std::string_view s(names);
        while (!s.empty()) {
            const size_t end = s.find(' ');
            t.emplace(s.substr(0, end), kind);
            s = end == std::string_view::npos ? "" : s.substr(end + 1);
        }
    }
    return t;
}();

const char *const CondNames[15] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs",
                                   "vc", "hi", "ls", "ge", "lt", "gt", "le",
                                   "al"};

bool
isCond(std::string_view s)
{
    for (const char *c : CondNames) {
        if (s == c)
            return true;
    }
    return false;
}

// A mnemonic's kind as m33width.py's kind_of() finds it.
Kind
kindOf(std::string_view mn)
{
    if (!mn.empty() && mn[0] == 'v')
        return Kind::Fp;
    const size_t n = mn.size();
    const bool s = n >= 1 && mn[n - 1] == 's';
    const bool cond = n >= 2 && isCond(mn.substr(n - 2));
    const std::string_view cands[] = {
        mn,
        s ? mn.substr(0, n - 1) : std::string_view(),
        cond ? mn.substr(0, n - 2) : std::string_view(),
        cond && n >= 3 && mn[n - 3] == 's' ? mn.substr(0, n - 3)
                                           : std::string_view(),
    };
    for (std::string_view c : cands) {
        if (c.empty())
            continue;
        auto it = KindTable.find(c);
        if (it != KindTable.end())
            return it->second;
    }
    if (n >= 1 && mn[0] == 'b' && isCond(mn.substr(1)))
        return Kind::Branch;
    return Kind::Other;
}

uint32_t
bits(uint32_t v, int hi, int lo)
{
    return (v >> lo) & ((1u << (hi - lo + 1)) - 1);
}

uint32_t
bit(uint32_t v, int b)
{
    return (v >> b) & 1;
}

struct Decoded
{
    std::string name;
    bool shiftOperand = false;  // an operand is shifted on the way in
    int cond = -1;              // a conditional branch's own condition
};

// The data-processing operations of the 32-bit shifted-register and
// modified-immediate encodings, by op (rn, rd and S pick tst, mov ...).
std::string
dataProcessing(uint32_t op, bool s, uint32_t rn, uint32_t rd)
{
    switch (op) {
      case 0: return rd == 15 && s ? "tst" : "and";
      case 1: return "bic";
      case 2: return rn == 15 ? "mov" : "orr";
      case 3: return rn == 15 ? "mvn" : "orn";
      case 4: return rd == 15 && s ? "teq" : "eor";
      case 8: return rd == 15 && s ? "cmn" : "add";
      case 10: return "adc";
      case 11: return "sbc";
      case 13: return rd == 15 && s ? "cmp" : "sub";
      case 14: return "rsb";
      default: return "undefined";
    }
}

Decoded
decode16(uint32_t hw)
{
    Decoded d;
    if (bits(hw, 15, 14) == 0) {
        switch (bits(hw, 13, 11)) {
          case 0: d.name = bits(hw, 10, 6) == 0 ? "mov" : "lsl"; break;
          case 1: d.name = "lsr"; break;
          case 2: d.name = "asr"; break;
          case 3: d.name = bit(hw, 9) ? "sub" : "add"; break;
          case 4: d.name = "mov"; break;
          case 5: d.name = "cmp"; break;
          case 6: d.name = "add"; break;
          default: d.name = "sub"; break;
        }
    } else if (bits(hw, 15, 10) == 0x10) {
        static const char *const ops[16] = {
            "and", "eor", "lsl", "lsr", "asr", "adc", "sbc", "ror",
            "tst", "neg", "cmp", "cmn", "orr", "mul", "bic", "mvn"};
        d.name = ops[bits(hw, 9, 6)];
    } else if (bits(hw, 15, 10) == 0x11) {
        switch (bits(hw, 9, 8)) {
          case 0: d.name = "add"; break;
          case 1: d.name = "cmp"; break;
          case 2: d.name = "mov"; break;
          default:
            d.name = bit(hw, 7) ? "blx" : "bx";
            if (bits(hw, 2, 0) == 4)
                d.name += "ns";
            break;
        }
    } else if (bits(hw, 15, 11) == 0x9) {
        d.name = "ldr";
    } else if (bits(hw, 15, 12) == 0x5) {
        static const char *const ops[8] = {"str", "strh", "strb", "ldrsb",
                                           "ldr", "ldrh", "ldrb", "ldrsh"};
        d.name = ops[bits(hw, 11, 9)];
    } else if (bits(hw, 15, 13) == 0x3) {
        static const char *const ops[4] = {"str", "ldr", "strb", "ldrb"};
        d.name = ops[bits(hw, 12, 11)];
    } else if (bits(hw, 15, 12) == 0x8) {
        d.name = bit(hw, 11) ? "ldrh" : "strh";
    } else if (bits(hw, 15, 12) == 0x9) {
        d.name = bit(hw, 11) ? "ldr" : "str";
    } else if (bits(hw, 15, 12) == 0xa) {
        d.name = "add";
    } else if (bits(hw, 15, 12) == 0xb) {
        const uint32_t op = bits(hw, 11, 5);
        if ((op & 0x78) == 0x00) {
            d.name = bit(hw, 7) ? "sub" : "add";
        } else if ((op & 0x28) == 0x08) {
            d.name = bit(hw, 11) ? "cbnz" : "cbz";
        } else if ((op & 0x78) == 0x10) {
            static const char *const ops[4] = {"sxth", "sxtb", "uxth",
                                               "uxtb"};
            d.name = ops[bits(hw, 7, 6)];
        } else if ((op & 0x70) == 0x20) {
            d.name = "push";
        } else if (op == 0x33) {
            d.name = bit(hw, 4) ? "cpsid" : "cpsie";
        } else if ((op & 0x78) == 0x50) {
            static const char *const ops[4] = {"rev", "rev16", "hlt",
                                               "revsh"};
            d.name = ops[bits(hw, 7, 6)];
        } else if ((op & 0x70) == 0x60) {
            d.name = "pop";
        } else if ((op & 0x78) == 0x70) {
            d.name = "bkpt";
        } else if ((op & 0x78) == 0x78) {
            if (bits(hw, 3, 0) != 0) {
                d.name = "it";
            } else {
                static const char *const hints[5] = {"nop", "yield", "wfe",
                                                     "wfi", "sev"};
                d.name = bits(hw, 7, 4) < 5 ? hints[bits(hw, 7, 4)] : "hint";
            }
        } else {
            d.name = "undefined";
        }
    } else if (bits(hw, 15, 12) == 0xc) {
        d.name = bit(hw, 11) ? "ldmia" : "stmia";
    } else if (bits(hw, 15, 12) == 0xd) {
        const uint32_t c = bits(hw, 11, 8);
        if (c == 14) {
            d.name = "udf";
        } else if (c == 15) {
            d.name = "svc";
        } else {
            d.name = "b";
            d.cond = c;
        }
    } else {
        d.name = "b";
    }
    return d;
}

Decoded
coprocessor(uint32_t hw1, uint32_t hw2)
{
    Decoded d;
    if ((bits(hw2, 11, 8) & 0xe) != 0xa) {
        d.name = "coprocessor";
        return d;
    }
    // Floating point (coprocessors 10 and 11).
    if (bits(hw1, 15, 9) == 0x76) {
        const uint32_t op = bits(hw1, 8, 4);
        if ((op & 0x1e) == 0x04)
            d.name = "vmov";
        else if (op == 0x02)
            d.name = "vlstm";
        else if (op == 0x03)
            d.name = "vlldm";
        else if (bit(hw1, 4))
            d.name = bit(hw1, 8) && !bit(hw1, 5) ? "vldr" : "vldm";
        else
            d.name = bit(hw1, 8) && !bit(hw1, 5) ? "vstr" : "vstm";
    } else {
        d.name = "vfp";
    }
    return d;
}

Decoded
decode32(uint32_t enc)
{
    const uint32_t hw1 = enc >> 16, hw2 = enc & 0xffff;
    const uint32_t op1 = bits(hw1, 12, 11), op2 = bits(hw1, 10, 4);
    Decoded d;
    d.name = "undefined";
    if (op1 == 1) {
        if ((op2 & 0x64) == 0x00) {
            // Load/store multiple.
            const bool load = bit(hw1, 4), wback = bit(hw1, 5);
            const bool sp = bits(hw1, 3, 0) == 13;
            switch (bits(hw1, 8, 7)) {
              case 1:
                d.name = load ? (wback && sp ? "pop" : "ldmia") : "stmia";
                break;
              case 2:
                d.name = load ? "ldmdb" : (wback && sp ? "push" : "stmdb");
                break;
              default:
                d.name = load ? "rfe" : "srs";
                break;
            }
        } else if ((op2 & 0x64) == 0x04) {
            // Load/store dual or exclusive, table branch.
            const uint32_t o1 = bits(hw1, 8, 7), o2 = bits(hw1, 5, 4);
            const uint32_t o3 = bits(hw2, 7, 4);
            if (enc == 0xe97fe97f) {
                d.name = "sg";
            } else if (o1 == 0 && o2 == 0) {
                d.name = bits(hw2, 15, 12) == 15 ? "tt" : "strex";
            } else if (o1 == 0 && o2 == 1) {
                d.name = "ldrex";
            } else if (((o1 & 2) == 0 && o2 == 2) ||
                       ((o1 & 2) && (o2 & 1) == 0)) {
                d.name = "strd";
            } else if (((o1 & 2) == 0 && o2 == 3) || ((o1 & 2) && (o2 & 1))) {
                d.name = "ldrd";
            } else if (o1 == 1 && o2 == 0) {
                static const char *const ops[16] = {
                    "", "", "", "", "strexb", "strexh", "", "",
                    "stlb", "stlh", "stl", "", "stlexb", "stlexh", "stlex",
                    ""};
                if (*ops[o3])
                    d.name = ops[o3];
            } else if (o1 == 1 && o2 == 1) {
                static const char *const ops[16] = {
                    "tbb", "tbh", "", "", "ldrexb", "ldrexh", "", "",
                    "ldab", "ldah", "lda", "", "ldaexb", "ldaexh", "ldaex",
                    ""};
                if (*ops[o3])
                    d.name = ops[o3];
                d.shiftOperand = o3 == 1;  // tbh [rn, rm, lsl #1]
            }
        } else if ((op2 & 0x60) == 0x20) {
            // Data processing (shifted register).
            const uint32_t op = bits(hw1, 8, 5), rn = bits(hw1, 3, 0);
            const uint32_t type = bits(hw2, 5, 4);
            const uint32_t imm = bits(hw2, 14, 12) << 2 | bits(hw2, 7, 6);
            const bool shifted = type != 0 || imm != 0;
            if (op == 6) {
                d.name = bit(hw2, 5) ? "pkhtb" : "pkhbt";
                d.shiftOperand = shifted;
            } else {
                d.name = dataProcessing(op, bit(hw1, 4), rn, bits(hw2, 11, 8));
                d.shiftOperand = shifted;
            }
        } else {
            d = coprocessor(hw1, hw2);
        }
    } else if (op1 == 2) {
        if (!bit(hw2, 15)) {
            if (!bit(hw1, 9)) {
                // Data processing (modified immediate).
                d.name = dataProcessing(bits(hw1, 8, 5), bit(hw1, 4),
                                        bits(hw1, 3, 0), bits(hw2, 11, 8));
            } else {
                // Data processing (plain binary immediate).
                const uint32_t imm = bits(hw2, 14, 12) << 2 | bits(hw2, 7, 6);
                switch (bits(hw1, 8, 4)) {
                  case 0x00: d.name = "addw"; break;
                  case 0x04: d.name = "movw"; break;
                  case 0x0a: d.name = "subw"; break;
                  case 0x0c: d.name = "movt"; break;
                  case 0x10:
                    d.name = "ssat";
                    d.shiftOperand = imm != 0;
                    break;
                  case 0x12:
                    d.name = imm == 0 ? "ssat16" : "ssat";
                    d.shiftOperand = imm != 0;
                    break;
                  case 0x14: d.name = "sbfx"; break;
                  case 0x16:
                    d.name = bits(hw1, 3, 0) == 15 ? "bfc" : "bfi";
                    break;
                  case 0x18:
                    d.name = "usat";
                    d.shiftOperand = imm != 0;
                    break;
                  case 0x1a:
                    d.name = imm == 0 ? "usat16" : "usat";
                    d.shiftOperand = imm != 0;
                    break;
                  case 0x1c: d.name = "ubfx"; break;
                  default: break;
                }
            }
        } else {
            // Branches and miscellaneous control.
            const uint32_t o1 = bits(hw2, 14, 12) & 5;
            if (o1 == 0) {
                if (bits(hw1, 9, 7) != 7) {
                    d.name = "b";
                    d.cond = bits(hw1, 9, 6);
                } else {
                    const uint32_t op = bits(hw1, 10, 4);
                    if ((op & 0x7e) == 0x38) {
                        d.name = "msr";
                    } else if (op == 0x3a) {
                        static const char *const hints[5] = {
                            "nop", "yield", "wfe", "wfi", "sev"};
                        d.name = bits(hw2, 7, 0) < 5 ? hints[bits(hw2, 7, 0)]
                                                     : "hint";
                    } else if (op == 0x3b) {
                        d.name = "barrier";
                    } else if ((op & 0x7e) == 0x3e) {
                        d.name = "mrs";
                    }
                }
            } else if (o1 == 1) {
                d.name = "b";
            } else if (o1 == 4) {
                d.name = "blx";
            } else {
                d.name = "bl";
            }
        }
    } else {
        if ((op2 & 0x71) == 0x00) {
            // Store single data item.
            static const char *const ops[4] = {"strb", "strh", "str", ""};
            const uint32_t size = bits(hw1, 6, 5);
            if (*ops[size]) {
                d.name = ops[size];
                if (!bit(hw1, 7) && bits(hw2, 11, 8) == 0xe)
                    d.name += "t";
                else if (!bit(hw1, 7) && bits(hw2, 11, 6) == 0)
                    d.shiftOperand = bits(hw2, 5, 4) != 0;
            }
        } else if ((op2 & 0x67) == 0x01 || (op2 & 0x67) == 0x03 ||
                   (op2 & 0x67) == 0x05) {
            // Load byte, halfword or word, memory hints.
            static const char *const ops[2][3] = {{"ldrb", "ldrh", "ldr"},
                                                  {"ldrsb", "ldrsh", ""}};
            const uint32_t size = bits(hw1, 6, 5);
            const bool sign = bit(hw1, 8), imm12 = bit(hw1, 7);
            const bool literal = bits(hw1, 3, 0) == 15;
            if (*ops[sign][size])
                d.name = ops[sign][size];
            if (size < 2 && bits(hw2, 15, 12) == 15)
                d.name = size == 0 ? (sign ? "pli" : "pld") : "hint";
            if (!literal && !imm12 && bits(hw2, 11, 8) == 0xe &&
                d.name.compare(0, 2, "ld") == 0) {
                d.name += "t";
            } else if (!literal && !imm12 && bits(hw2, 11, 6) == 0) {
                d.shiftOperand = bits(hw2, 5, 4) != 0;
            }
        } else if ((op2 & 0x70) == 0x20) {
            // Data processing (register).
            const uint32_t o1 = bits(hw1, 7, 4), o2 = bits(hw2, 7, 4);
            const bool rn15 = bits(hw1, 3, 0) == 15;
            if ((o1 & 8) == 0 && o2 == 0) {
                static const char *const shifts[4] = {"lsl", "lsr", "asr",
                                                      "ror"};
                d.name = shifts[bits(o1, 2, 1)];
            } else if ((o1 & 8) == 0 && (o2 & 8)) {
                static const char *const ext[6] = {"sxth", "uxth", "sxtb16",
                                                   "uxtb16", "sxtb", "uxtb"};
                if (o1 < 6) {
                    d.name = ext[o1];
                    if (!rn15)
                        d.name.insert(3, "a");
                    d.shiftOperand = bits(hw2, 5, 4) != 0;
                }
            } else if ((o1 & 8) && (o2 & 8) == 0) {
                // Parallel addition and subtraction.
                static const char *const pre[2][3] = {{"s", "q", "sh"},
                                                      {"u", "uq", "uh"}};
                static const char *const ops[8] = {"add8", "add16", "asx", "",
                                                   "sub8", "sub16", "sax", ""};
                const uint32_t p = bits(o2, 1, 0), o = bits(o1, 2, 0);
                if (p < 3 && *ops[o])
                    d.name = std::string(pre[bit(o2, 2)][p]) + ops[o];
            } else if ((o1 & 0xc) == 8 && (o2 & 0xc) == 8) {
                static const char *const ops[4][4] = {
                    {"qadd", "qdadd", "qsub", "qdsub"},
                    {"rev", "rev16", "rbit", "revsh"},
                    {"sel", "", "", ""},
                    {"clz", "", "", ""}};
                const char *n = ops[bits(o1, 1, 0)][bits(o2, 1, 0)];
                if (*n)
                    d.name = n;
            }
        } else if ((op2 & 0x78) == 0x30) {
            // Multiply, multiply accumulate, absolute difference.
            static const char *const xy[4] = {"bb", "bt", "tb", "tt"};
            const uint32_t o2 = bits(hw2, 5, 4);
            const bool acc = bits(hw2, 15, 12) != 15;
            const char *x = bit(hw2, 4) ? "x" : "";
            switch (bits(hw1, 6, 4)) {
              case 0:
                if (o2 < 2)
                    d.name = o2 ? "mls" : acc ? "mla" : "mul";
                break;
              case 1:
                d.name = std::string(acc ? "smla" : "smul") + xy[o2];
                break;
              case 2:
                d.name = std::string(acc ? "smlad" : "smuad") + x;
                break;
              case 3:
                d.name = std::string(acc ? "smlaw" : "smulw") +
                         (bit(hw2, 4) ? "t" : "b");
                break;
              case 4:
                d.name = std::string(acc ? "smlsd" : "smusd") + x;
                break;
              case 5:
                d.name = std::string(acc ? "smmla" : "smmul") +
                         (bit(hw2, 4) ? "r" : "");
                break;
              case 6:
                d.name = std::string("smmls") + (bit(hw2, 4) ? "r" : "");
                break;
              default:
                if (o2 == 0)
                    d.name = acc ? "usada8" : "usad8";
                break;
            }
        } else if ((op2 & 0x78) == 0x38) {
            // Long multiply, long multiply accumulate, divide.
            const uint32_t o2 = bits(hw2, 7, 4);
            switch (bits(hw1, 6, 4)) {
              case 0: if (o2 == 0) d.name = "smull"; break;
              case 1: if (o2 == 15) d.name = "sdiv"; break;
              case 2: if (o2 == 0) d.name = "umull"; break;
              case 3: if (o2 == 15) d.name = "udiv"; break;
              case 4:
                if (o2 == 0)
                    d.name = "smlal";
                else if ((o2 & 0xc) == 8)
                    d.name = "smlal" + std::string(
                        bit(o2, 1) ? "t" : "b") + (bit(o2, 0) ? "t" : "b");
                else if ((o2 & 0xe) == 0xc)
                    d.name = "smlald";
                break;
              case 5: if ((o2 & 0xe) == 0xc) d.name = "smlsld"; break;
              case 6:
                if (o2 == 0)
                    d.name = "umlal";
                else if (o2 == 6)
                    d.name = "umaal";
                break;
              default: break;
            }
        } else if (op2 & 0x40) {
            d = coprocessor(hw1, hw2);
        }
    }
    return d;
}

Decoded
decode(uint32_t enc, unsigned width)
{
    return width == 4 ? decode32(enc) : decode16(enc & 0xffff);
}

std::string
printed(const Decoded &d, int itCond)
{
    const int cond = d.cond >= 0 ? d.cond : itCond;
    return cond >= 0 && cond < 14 ? d.name + CondNames[cond] : d.name;
}

} // anonymous namespace

std::string
mnemonic(uint32_t enc, unsigned width, int itCond)
{
    return printed(decode(enc, width), itCond);
}

Traits
traits(uint32_t enc, unsigned width, int itCond)
{
    const Decoded d = decode(enc, width);
    const std::string mn = printed(d, itCond);
    Traits t;
    t.kind = kindOf(mn);
    const bool shiftName = mn == "lsl" || mn == "lsr" || mn == "asr" ||
                           mn == "ror" || mn == "rrx";
    t.shift = d.shiftOperand || shiftName;
    static const char *const writes[] = {"str", "stm", "push", "vstr",
                                         "vstm", "vpush"};
    t.write = false;
    for (const char *w : writes) {
        if (mn.compare(0, std::char_traits<char>::length(w), w) == 0)
            t.write = true;
    }
    return t;
}

} // namespace thumb
} // namespace gem5
