#ifndef __DEV_ARM_M_PROFILE_COPROCESSOR_HH__
#define __DEV_ARM_M_PROFILE_COPROCESSOR_HH__

#include "arch/arm/m_coproc_access.hh"
#include "params/MProfileCoprocessor.hh"
#include "sim/sim_object.hh"

namespace gem5
{

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
