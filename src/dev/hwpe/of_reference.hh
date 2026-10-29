#ifndef __DEV_HWPE_OF_REFERENCE_HH__
#define __DEV_HWPE_OF_REFERENCE_HH__

#include <cstdint>
#include <memory>

namespace gem5
{

// The optical-flow engines' arithmetic from arcsim-kernels (klt/lk.h, fixed
// point), which needs C++20: of_reference.cc is built as C++20 from
// $ARCSIM_KERNELS_HOME/include.
namespace of_reference
{

// The window side of the LK engine (lkPackage's WIN)
constexpr int lkWin = 15;
// The template's block side and the window's
constexpr int lkBlock = lkWin + 3;
constexpr int lkWindow = lkWin + 1;
// The rotated LK engine's patch side (lkPackage's ROT_SIDE) and its reach
constexpr int lkRotSide = 22;
constexpr int lkRotReach = (lkRotSide - 4) / 2;

// lkTemplate's output: the template of a level and its solve
class LkTemplate
{
  public:
    LkTemplate();
    ~LkTemplate();

    // The template at (px, py) (top-left, level coordinates, Q14) of a
    // width x height level from its lkBlock x lkBlock block at
    // ((px >> 14) - 1, (py >> 14) - 1); false if the solve rejects it.
    bool make(const uint8_t *block, int32_t px, int32_t py, int width,
              int height, int64_t min_eig, int b_shift);

    // lkTemplateRotated: the template through the map m (Q14, row-major)
    // around the point (pcx, pcy) (level coordinates, Q14) from its
    // lkRotSide x lkRotSide block at ((pcx >> 14) - lkRotReach - 1,
    // (pcy >> 14) - lkRotReach - 1)
    bool makeMapped(const uint8_t *block, int32_t pcx, int32_t pcy,
                    const int32_t m[4], int width, int height,
                    int64_t min_eig, int b_shift);

    // lkMismatch: the sums of the lkWindow x lkWindow window at the
    // window position (qx, qy) (Q14) against the template
    void mismatch(const uint8_t *window, int32_t qx, int32_t qy,
                  int64_t &b1, int64_t &b2) const;

    struct Step
    {
        int32_t dx, dy;
        bool converged, oscillating;
    };
    // lkStep
    Step step(int64_t b1, int64_t b2, int32_t pdx, int32_t pdy, bool first,
              int64_t eps2, int b_shift) const;

  private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace of_reference
} // namespace gem5

#endif // __DEV_HWPE_OF_REFERENCE_HH__
