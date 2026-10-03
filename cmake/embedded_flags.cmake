# Embedded (rp2350) compile-flag pass. Directory-scoped so FetchContent libraries added afterward
# inherit it; call after pico_sdk_init().
#
#   -fno-unwind-tables, -fno-asynchronous-unwind-tables: no unwinder is linked.
#   -fno-exceptions, -fno-rtti: docs/coding-style.md, and no __cxa_* runtime.
#   -fno-threadsafe-statics: no guard-variable locking on function-local statics.
#   -Wdouble-promotion: the Cortex-M33 FPU is single precision; an accidental promotion becomes a
#     soft-float call.
function(hp2_apply_embedded_flags)
  add_compile_options(
    -fno-unwind-tables -fno-asynchronous-unwind-tables $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti> $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    $<$<COMPILE_LANGUAGE:CXX>:-Wdouble-promotion>)
endfunction()
