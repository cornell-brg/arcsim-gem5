/**
 * @file
 * Declares an STM32 flash behind its accelerator (ART), one read at a time.
 */

#ifndef __MEM_ART_FLASH_HH__
#define __MEM_ART_FLASH_HH__

#include "mem/simple_mem.hh"
#include "params/ARTFlash.hh"

namespace gem5
{

namespace memory
{

/**
 * An STM32 flash behind its accelerator: one read at a time, each holding
 * the flash for readTime (RM0090 3.5.2). A data read waits for the read in
 * progress and goes before a prefetch that has not started (D-Code before
 * I-Code). A fetch miss drops the prefetch, read or not, the stream having
 * moved (a miss costs the wait states).
 */
class ARTFlash : public SimpleMemory
{
  private:
    const Tick readTime;
    /** When the last read scheduled ends. */
    Tick freeAt = 0;
    /** When the last demand read scheduled ends. */
    Tick demandFreeAt = 0;
    /** The prefetch last scheduled, when its read starts, and its
     *  response's delay after that. */
    PacketPtr pfPkt = nullptr;
    Tick pfStart = 0;
    Tick pfRespOffset = 0;

    void insertResponse(PacketPtr pkt, Tick when);

  public:
    ARTFlash(const ARTFlashParams &p);

  protected:
    bool recvTimingReq(PacketPtr pkt) override;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_ART_FLASH_HH__
