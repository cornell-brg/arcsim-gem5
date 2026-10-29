from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class HCIMux(ClockedObject):
    """
    hci_core_mux_dynamic between HWPEs and one TCDM port, as arcsim-xcel's
    optical-flow wrapper joins its engines (hwpe-optical-flow.v): input n is
    the mux's channel n, of num_channels (a power of two; channels without
    an input never request).

    Each input holds one request until it is granted; a further request is
    refused and retried after the grant. Once a cycle, after the inputs'
    requests of the cycle are in, the last requesting input in the order
    rr, rr + 1, ... wins and its request goes to the TCDM; rr steps once
    per granted request. A request the TCDM refuses waits there for its
    retry, and nothing else is sent until then. Responses go back to the
    input that sent the request.
    """

    type = "HCIMux"
    cxx_class = "gem5::HCIMux"
    cxx_header = "dev/hwpe/hci_mux.hh"

    cpu_side_ports = VectorResponsePort("The HWPEs' TCDM ports, channel n on port n")
    mem_side_port = RequestPort("To the TCDM's HWPE port")
    num_channels = Param.Unsigned(4, "Channels of the mux (NB_IN_CHAN)")
