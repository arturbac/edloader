# The chain test's dlls (tools/test.sh runs them under wine): fake_a and fake_b (one source, a name each),
# fake_intruder (takes the game's import in its DllMain), fake_hooker (hooks edloader's own export, as 3Dmigoto does).
set(test_out "${CMAKE_BINARY_DIR}/test")

function(edloader_fake name source)
  add_library(${name} SHARED ${source})
  target_compile_definitions(${name} PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS ${ARGN})
  # edloader routes a call by its caller's return address; clang turns a fake's `return create(...)` into a
  # tail jump, so the call reaches edloader from the fake's own caller and the chain stops after the fake
  target_compile_options(${name} PRIVATE /W4 /clang:-fno-optimize-sibling-calls)
  set_target_properties(${name} PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${test_out}"
    ARCHIVE_OUTPUT_DIRECTORY "${test_out}/lib")
endfunction()

foreach(n a b)
  edloader_fake(fake_${n} fake_proxy.cc "FAKE_NAME=\"fake_${n}\"")
  target_link_options(fake_${n} PRIVATE /EXPORT:D3D11CreateDevice=fake_D3D11CreateDevice)
endforeach()
edloader_fake(fake_intruder fake_intruder.cc)
edloader_fake(fake_hooker fake_hooker.cc)
