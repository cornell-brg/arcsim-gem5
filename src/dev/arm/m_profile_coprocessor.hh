#ifndef __DEV_ARM_M_PROFILE_COPROCESSOR_HH__
#define __DEV_ARM_M_PROFILE_COPROCESSOR_HH__

#include <cstdint>

#include "params/MProfileCoprocessor.hh"
#include "sim/sim_object.hh"

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

// A coprocessor on the Cortex-M coprocessor port (see MProfileCoprocessor.py).
// Registers itself with the ArmMSystem for each of its numbers.
class MProfileCoprocessor : public SimObject
{
  public:
    PARAMS(MProfileCoprocessor);
    MProfileCoprocessor(const Params &p);

    // Performs the access; false if it is not modelled, which stops the
    // simulation.
    virtual bool access(MProfileCoprocAccess &a) { return false; }
};

} // namespace gem5

#endif // __DEV_ARM_M_PROFILE_COPROCESSOR_HH__
