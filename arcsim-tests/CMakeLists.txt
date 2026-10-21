cmake_minimum_required(VERSION 3.24)

# Runs soc's tests on gem5's RP2350 board (configs/example/rp2350/run.py) with
# the arcsim test framework, as soc/tests runs them on rtl-sim. The ELFs are
# built in soc ($ARCSIM_SOC_HOME/tests); this project only lists and runs them.
# Needs ARCSIM_TEST_HOME (source setup-arcsim.sh in an arcsim clone) and
# ARCSIM_SOC_HOME (module load arcsim-soc).
project(arcsim-gem5-tests NONE)
include(CTest)

foreach(var ARCSIM_TEST_HOME ARCSIM_SOC_HOME)
  if(NOT DEFINED ENV{${var}})
    message(FATAL_ERROR "${var} is not set (source setup-arcsim.sh in an arcsim clone, module load arcsim-soc)")
  endif()
endforeach()
set(ARCSIM_TEST_HOME $ENV{ARCSIM_TEST_HOME})

# The one place that knows where soc keeps its built tests.
set(SOC_TESTS $ENV{ARCSIM_SOC_HOME}/tests/build CACHE PATH "soc's test build directory (*.elf)")
set(GEM5_RISCV ${CMAKE_CURRENT_SOURCE_DIR}/../build/RISCV/gem5.opt CACHE FILEPATH "gem5 binary the tests run on")
set(RUN_PY ${CMAKE_CURRENT_SOURCE_DIR}/../configs/example/rp2350/run.py)

# Every run stops at TICK_LIMIT, so a hanging test fails (run.py returns -1,
# as rtl-sim on --cycles) instead of blocking until CTest's timeout.
set(TICK_LIMIT 100000000000 CACHE STRING "--tick-limit of every gem5 run")

# hazard3_test(<soc test> [<run.py arg>...]): runs <soc test>.elf on the
# hazard3-proxy as test hazard3_<soc test>. Each test has its own gem5 output
# directory, so tests can run in parallel (ctest -j).
function(hazard3_test name)
  set(test hazard3_${name})
  set(elf ${SOC_TESTS}/${name}.elf)
  add_test(NAME ${test}
           COMMAND sh ${ARCSIM_TEST_HOME}/bin/arcsim-test-run
                   ${CMAKE_BINARY_DIR}/test-logs/${test}.log
                   sh -c "test -f \"${elf}\" || { echo \"${elf} not built: make -C \$ARCSIM_SOC_HOME/tests build\"; exit 1; }; exec \"$0\" \"$@\""
                   ${GEM5_RISCV} --outdir=${CMAKE_BINARY_DIR}/m5out/${test}
                   ${RUN_PY} --core hazard3-proxy --model accurate --sram-model banked
                   --firmware ${elf} --tick-limit ${TICK_LIMIT} ${ARGN})
  set_tests_properties(${test} PROPERTIES LABELS "hazard3;riscv")
endfunction()

hazard3_test(test_memory)
hazard3_test(test_interconnect)
# Needs the HWPE (0x50800000), still a placeholder on the board
hazard3_test(test_vvadd)
hazard3_test(test_dma)
