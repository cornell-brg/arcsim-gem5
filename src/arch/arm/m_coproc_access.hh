#ifndef __ARCH_ARM_M_COPROC_ACCESS_HH__
#define __ARCH_ARM_M_COPROC_ACCESS_HH__

#include <cstdint>

namespace gem5
{

// One coprocessor instruction as the coprocessor sees it. For writes (MCR,
// MCRR) rt and rt2 hold the core registers; for reads (MRC, MRRC) the
// coprocessor fills them. CDP carries CRd in rt.
struct MProfileCoprocAccess
{
    enum class Kind { Mcr, Mrc, Mcrr, Mrrc, Cdp };

    Kind kind;
    // The hw1[12] forms: MCR2, MRC2, MCRR2, MRRC2, CDP2.
    bool two;
    unsigned coproc;
    unsigned opc1;
    unsigned crn;
    unsigned crm;
    unsigned opc2;
    uint32_t rt = 0;
    uint32_t rt2 = 0;
};

} // namespace gem5

#endif // __ARCH_ARM_M_COPROC_ACCESS_HH__
