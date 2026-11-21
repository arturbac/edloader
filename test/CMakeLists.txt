# The chain test's dlls (tools/test.sh runs them under wine): fake_a and fake_b (one source, a name each), fake_swap
# (the same with D3D11CreateDeviceAndSwapChain too, for swap_app, a game asking for that call),
# fake_intruder (takes the game's import in its DllMain), fake_hooker (hooks edloader's own export, as 3Dmigoto does),
# fake_reshade (exports ReShadeRegisterAddon and hooks the system d3d11 inline through a stub of no module, as ReShade does),
# fake_jumper (hooks the system d3d11 inline with jmp [rip+rel32], rel32 far from 0, through a pointer near the function).
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
edloader_fake(fake_swap fake_proxy.cc "FAKE_NAME=\"fake_swap\"" FAKE_SWAP)
target_link_options(fake_swap PRIVATE /EXPORT:D3D11CreateDevice=fake_D3D11CreateDevice
                    /EXPORT:D3D11CreateDeviceAndSwapChain=fake_D3D11CreateDeviceAndSwapChain)
edloader_fake(fake_intruder fake_intruder.cc)
edloader_fake(fake_hooker fake_hooker.cc)
edloader_fake(fake_reshade fake_reshade.cc)
edloader_fake(fake_jumper fake_jumper.cc)

add_executable(swap_app swap_app.cc)
target_compile_definitions(swap_app PRIVATE WIN32_LEAN_AND_MEAN)
target_compile_options(swap_app PRIVATE /W4)
set_target_properties(swap_app PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${test_out}")
