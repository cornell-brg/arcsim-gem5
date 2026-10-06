#include "dev/arm/m_profile_coprocessor.hh"

#include "arch/arm/m_system.hh"
#include "base/logging.hh"

namespace gem5
{

MProfileCoprocessor::MProfileCoprocessor(const Params &p) : SimObject(p)
{
    // Registered in the constructor, like the SCS, so the table is complete
    // before any core runs.
    for (unsigned n : p.numbers) {
        fatal_if(n > 7, "%s: coprocessor number %u is not 0-7", name(), n);
        p.system->setCoprocessor(n, this);
    }
}

} // namespace gem5
