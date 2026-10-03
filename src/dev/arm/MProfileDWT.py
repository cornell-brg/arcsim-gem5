# Copyright (c) 2026 Cornell University
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.objects.Device import BasicPioDevice
from m5.objects.MProfileSCS import MProfileSCS
from m5.params import *
from m5.proxy import *


class MProfileDWT(BasicPioDevice):
    """
    M-profile Data Watchpoint and Trace (DWT) unit: the CYCCNT cycle
    counter only.

    Models the 4KB block at the architecturally fixed address
    0xE0001000 (DDI0403E C1.8).  CYCCNT counts cycles of this
    device's clock domain, so attach it to the core clock domain.
    As on silicon, the counter runs only while both DEMCR.TRCENA
    (in the SCS) and DWT_CTRL.CYCCNTENA are set.  The other profiling
    counters, comparators and the PC sampler are not modeled: their
    registers accept writes and read as zero.
    """

    type = "MProfileDWT"
    cxx_class = "gem5::MProfileDWT"
    cxx_header = "dev/arm/m_profile_dwt.hh"

    pio_addr = 0xE0001000

    # The SCS owns DEMCR.TRCENA, the trace-enable gate for CYCCNT.
    # Defaults to the sibling 'scs' of the platform this device lives in.
    scs = Param.MProfileSCS(Parent.scs, "M-profile SCS that owns DEMCR")
