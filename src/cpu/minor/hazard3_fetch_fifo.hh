#ifndef __CPU_MINOR_HAZARD3_FETCH_FIFO_HH__
#define __CPU_MINOR_HAZARD3_FETCH_FIFO_HH__

#include <cassert>
#include <deque>

namespace gem5::minor
{

/** Occupancy control from hazard3_frontend: two fetched words and the
 *  separate three-halfword instruction assembly register. A word may carry
 *  one valid halfword at a branch target or after a predicted branch. */
class Hazard3FetchFifo
{
  private:
    std::deque<unsigned> words;
    unsigned assembly = 0;

  public:
    unsigned fifoWords() const { return words.size(); }
    unsigned assemblyHalfwords() const { return assembly; }

    bool stalled(unsigned pending) const
    {
        return words.size() == 2 || (!words.empty() && pending != 0) ||
            pending > 1;
    }

    void flush(unsigned retainedHalfwords = 0)
    {
        words.clear();
        assembly = retainedHalfwords;
    }

    /** Advance one clock after sampling stalled(). The current response
     *  can bypass an empty FIFO; a popped FIFO word makes room for a
     *  simultaneous response, but neither frees an admission slot early. */
    void advance(unsigned consumed, unsigned returned)
    {
        assert(consumed <= assembly);
        assert(returned <= 2);
        assembly -= consumed;
        unsigned fetch = words.empty() ? returned : words.front();
        bool room = assembly <= (fetch == 1 ? 2 : 1);
        bool bypass = words.empty() && room;
        if (room && fetch) {
            assembly += fetch;
            if (!words.empty())
                words.pop_front();
        }
        if (returned && !bypass)
            words.push_back(returned);
        assert(words.size() <= 2);
        assert(assembly <= 3);
    }
};

} // namespace gem5::minor

#endif
