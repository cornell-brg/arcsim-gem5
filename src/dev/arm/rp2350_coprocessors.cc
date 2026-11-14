#include "dev/arm/rp2350_coprocessors.hh"

#include "dev/rp2350/rp2350_sio.hh"

namespace gem5
{

using Kind = MProfileCoprocAccess::Kind;
using GpioReg = RP2350SIO::GpioReg;
using GpioOp = RP2350SIO::GpioOp;

RP2350GpioCoprocessor::RP2350GpioCoprocessor(const Params &p)
    : MProfileCoprocessor(p), sio(p.sio)
{
}

bool
RP2350GpioCoprocessor::access(MProfileCoprocAccess &a)
{
    // RP2350 datasheet 3.6.1. CRm selects the register: c0/c1 OUT lo/hi,
    // c4/c5 OE lo/hi, c8/c9 IN lo/hi (reads only).
    if (a.two || a.crn != 0)
        return false;
    const bool oe = a.crm == 4 || a.crm == 5;
    const GpioReg lo = oe ? GpioReg::OeLo : GpioReg::OutLo;
    const GpioReg hi = oe ? GpioReg::OeHi : GpioReg::OutHi;
    // opc1 0-3 and the index forms 8-11: put, xor, set, clr.
    static const GpioOp maskOps[] = {
        GpioOp::Put, GpioOp::Xor, GpioOp::Set, GpioOp::Clr};
    // Single-bit opc1 5-7: xor, set, clr of pin Rt (mod 64).
    auto bitWrite = [&](GpioOp op, unsigned pin, uint32_t bit) {
        pin &= 63;
        sio->gpioWrite(pin < 32 ? lo : hi, op, bit << (pin & 31));
    };

    switch (a.kind) {
      case Kind::Mcr:
        if (a.opc2 != 0)
            return false;
        if (a.opc1 <= 3 && (a.crm == 0 || a.crm == 1 || oe)) {
            const bool isHi = a.crm == 1 || a.crm == 5;
            sio->gpioWrite(isHi ? hi : lo, maskOps[a.opc1], a.rt);
            return true;
        }
        if (a.opc1 >= 5 && a.opc1 <= 7 && (a.crm == 0 || a.crm == 4)) {
            bitWrite(maskOps[a.opc1 - 4], a.rt, 1);
            return true;
        }
        return false;

      case Kind::Mcrr:
        if (a.crm != 0 && a.crm != 4)
            return false;
        if (a.opc1 <= 3) {
            sio->gpioWrite(lo, maskOps[a.opc1], a.rt);
            sio->gpioWrite(hi, maskOps[a.opc1], a.rt2);
            return true;
        }
        if (a.opc1 == 4) {
            // Write Rt2[0] to pin Rt.
            bitWrite((a.rt2 & 1) ? GpioOp::Set : GpioOp::Clr, a.rt, 1);
            return true;
        }
        if (a.opc1 <= 7) {
            bitWrite(maskOps[a.opc1 - 4], a.rt, a.rt2 & 1);
            return true;
        }
        if (a.opc1 <= 11) {
            // Rt[0] selects the register and Rt is the value, as the RP2350
            // does (Rt2 is ignored).
            sio->gpioWrite((a.rt & 1) ? hi : lo, maskOps[a.opc1 - 8], a.rt);
            return true;
        }
        return false;

      case Kind::Mrc:
        if (a.opc1 != 0 || a.opc2 != 0)
            return false;
        switch (a.crm) {
          case 0: a.rt = sio->gpioRead(GpioReg::OutLo); return true;
          case 1: a.rt = sio->gpioRead(GpioReg::OutHi); return true;
          case 4: a.rt = sio->gpioRead(GpioReg::OeLo); return true;
          case 5: a.rt = sio->gpioRead(GpioReg::OeHi); return true;
          case 8: a.rt = sio->gpioRead(GpioReg::InLo); return true;
          case 9: a.rt = sio->gpioRead(GpioReg::InHi); return true;
        }
        return false;

      case Kind::Mrrc:
        if (a.opc1 != 0)
            return false;
        switch (a.crm) {
          case 0:
            a.rt = sio->gpioRead(GpioReg::OutLo);
            a.rt2 = sio->gpioRead(GpioReg::OutHi);
            return true;
          case 4:
            a.rt = sio->gpioRead(GpioReg::OeLo);
            a.rt2 = sio->gpioRead(GpioReg::OeHi);
            return true;
          case 8:
            a.rt = sio->gpioRead(GpioReg::InLo);
            a.rt2 = sio->gpioRead(GpioReg::InHi);
            return true;
        }
        return false;

      case Kind::Cdp:
        return false;
    }
    return false;
}

bool
RP2350DcpCoprocessor::access(MProfileCoprocAccess &a)
{
    // mrc p4, #0, Rt, c0, c0, #1: RCMP, which the SDK reads once at start-up
    // to clear the engaged flag; 0 with nothing engaged.
    if (a.kind == Kind::Mrc && !a.two && a.opc1 == 0 && a.crn == 0 &&
        a.crm == 0 && a.opc2 == 1) {
        a.rt = 0;
        return true;
    }
    return false;
}

} // namespace gem5
