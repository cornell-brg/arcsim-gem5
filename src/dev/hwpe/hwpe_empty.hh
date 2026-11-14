#ifndef __DEV_HWPE_HWPE_EMPTY_HH__
#define __DEV_HWPE_HWPE_EMPTY_HH__

#include "dev/hwpe/hwpe_base.hh"
#include "params/HWPEEmpty.hh"

namespace gem5
{

// An HWPE without an engine (see HWPE.py).
class HWPEEmpty : public HWPEBase
{
  public:
    PARAMS(HWPEEmpty);
    HWPEEmpty(const Params &p) : HWPEBase(p) {}

  protected:
    void startJob(const std::vector<uint32_t> &io_regs) override;
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_EMPTY_HH__
