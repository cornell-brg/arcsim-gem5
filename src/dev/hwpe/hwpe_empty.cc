#include "dev/hwpe/hwpe_empty.hh"

#include "base/trace.hh"
#include "debug/HWPE.hh"

namespace gem5
{

void
HWPEEmpty::startJob(const std::vector<uint32_t> &io_regs)
{
    // Nothing to compute and no TCDM accesses: the job is done at once
    DPRINTF(HWPE, "No engine: the job ends as it starts\n");
    jobDone();
}

} // namespace gem5
