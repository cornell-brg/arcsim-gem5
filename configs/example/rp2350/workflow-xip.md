# Frozen agentic workflow XIP configuration

`workflow-xip-final.json` is the unchanged configuration selected by the
completed workflow. Pass it to the arcsim study host runner with
`2_run.py ... model --tag final ... --xip-configuration PATH` together with
the matching source/binary freeze record. The workflow and host adapter live
in arcsim's `agentic-workflow-study/xip/`; the reusable procedural skill is
`mf-agent-workflows/xip-calibrate/`.

The configuration supplies board arguments for `make_board`, including
`xip_model="rp2350"`. `--model accurate` alone retains the legacy XIP selector
on this baseline. The validated study adapter is `run_gem5_study.py`; the
standalone legacy `run.py` is not the validated firmware adapter for this model.

The agent-produced implementation is commit
`1029a0e03268593b43d020c3d05ab2dc3a0ad172`, based on exact starting revision
`eb9c43214200daa618c39a92dffb9ab4675b0397`. This packaging follow-up adds only
the frozen configuration and this usage note; it does not retune the model.
CPU and other non-XIP implementations remain those of the approved baseline.

Configuration SHA256:
`40eee379c551c418870313604d71363faa50a3fba3d46ccefa610a7362b9ad3a`.
