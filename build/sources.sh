# Source lists per target, shared by build_pc.sh and build_r36s.sh (sourced, not executed).
GAME="src/game/scene.cpp src/game/models.cpp src/game/context.cpp src/game/rows.cpp src/game/player.cpp src/game/game_map.cpp src/game/game.cpp src/game/script.cpp"
# SDL-free engine code shared with the SF2000 core, and the SDL/GL platform layer of the PC and R36S builds
ENGINE_SHARED="src/engine/math.cpp src/engine/assets.cpp src/engine/log.cpp src/engine/gsap.cpp src/engine/input.cpp src/engine/audio.cpp src/engine/stb_vorbis_impl.cpp src/engine/config.cpp"
ENGINE_SDL="src/engine/platform_paths_sdl.cpp src/engine/gl_api.cpp src/engine/png_write.cpp src/engine/platform.cpp src/engine/renderer.cpp src/engine/input_sdl.cpp src/engine/audio_sdl.cpp src/engine/text.cpp"
ENGINE="$ENGINE_SHARED $ENGINE_SDL"
# the SF2000 subset (CR_FIXED, no float at all): the mixer plays MS ADPCM music instead of stb_vorbis, and has no
# device of its own (sf2000/audio_retro.cpp)
ENGINE_FIXED="src/engine/math.cpp src/engine/assets.cpp src/engine/log.cpp src/engine/gsap.cpp src/engine/input.cpp src/engine/config.cpp src/engine/audio.cpp src/engine/adpcm.cpp src/sf2000/audio_retro.cpp"
# the SF2000 software renderer (CR_FIXED only)
SW="src/sw/sw_math.cpp src/sw/raster.cpp src/sw/renderer_sw.cpp"
# what the renderer needs from the engine when linked on its own (tests)
SW_DEPS="src/engine/log.cpp src/engine/assets.cpp src/sf2000/platform_paths_sf2000.cpp"
SW_SCENE="src/sw/scene_render_sw.cpp"
# the HUD and screens (shared with the R36S build; real/mreal numbers)
UI="src/engine/text.cpp src/ui/hud.cpp src/ui/screens.cpp src/ui/lang.cpp src/ui/ranks.cpp src/ui/controls.cpp"
# what the logic-only tools need
LOGIC="$GAME src/engine/math.cpp src/engine/assets.cpp src/engine/log.cpp src/engine/gsap.cpp src/engine/platform_paths_sdl.cpp"

sources_for() {
  case "$1" in
    probe) echo "probe/main.cpp src/engine/gl_api.cpp src/engine/png_write.cpp" ;;
    test_math) echo "tests/test_math.cpp src/engine/math.cpp" ;;
    test_gsap) echo "tests/test_gsap.cpp src/engine/gsap.cpp" ;;
    test_rng) echo "tests/test_rng.cpp" ;;
    test_config) echo "tests/test_config.cpp src/engine/config.cpp" ;;
    test_easing) echo "tests/test_easing.cpp" ;;
    test_fixed) echo "tests/test_fixed.cpp" ;;
    # O23: Input's per-device masks (a device must be only itself) and playerDevice
    test_input) echo "tests/test_input.cpp src/engine/input.cpp src/ui/controls.cpp src/ui/screens.cpp src/ui/lang.cpp src/ui/ranks.cpp src/engine/log.cpp $GAME src/engine/math.cpp src/engine/assets.cpp src/engine/gsap.cpp src/engine/text.cpp src/engine/renderer.cpp src/engine/gl_api.cpp src/engine/platform_paths_sdl.cpp" ;;
    # SF2000 software renderer tests: build_pc.sh adds -DCR_FIXED for test_sw_*
    test_sw_math) echo "tests/test_sw_math.cpp $SW $SW_DEPS" ;;
    test_sw_raster) echo "tests/test_sw_raster.cpp $SW $SW_DEPS" ;;
    # the SF2000 audio path: MS ADPCM decoding and the 22050 Hz mixer (build/test_sw_audio.sh)
    test_sw_audio) echo "tests/test_sw_audio.cpp src/engine/audio.cpp src/engine/adpcm.cpp src/sf2000/audio_retro.cpp $SW_DEPS" ;;
    # the 16.16 game drawn by the software renderer: scenario screenshots (build/sw_scene_compare.sh)
    sw_game) echo "apps/sw_game.cpp $SW $SW_SCENE $UI src/engine/input.cpp $GAME src/engine/math.cpp src/engine/assets.cpp src/engine/log.cpp src/engine/gsap.cpp src/engine/png_write.cpp src/sf2000/platform_paths_sf2000.cpp" ;;
    # the model contact sheet drawn by the software renderer (compare with viewer --flat)
    sw_viewer) echo "apps/sw_viewer.cpp $SW src/engine/assets.cpp src/engine/log.cpp src/engine/png_write.cpp src/engine/math.cpp src/sf2000/platform_paths_sf2000.cpp" ;;
    # the Amiga sprite baker (docs/PLAN_AMIGA68K.md C1): every model through the game's own camera, cropped, with
    # anchors. Named sw_* so build_pc.sh gives it -DCR_FIXED (the software renderer is 16.16 only).
    sw_bake_amiga) echo "apps/sw_bake_amiga.cpp $SW src/engine/assets.cpp src/engine/log.cpp src/engine/png_write.cpp src/engine/math.cpp src/sf2000/platform_paths_sf2000.cpp" ;;
    test_audio) echo "tests/test_audio.cpp src/engine/audio.cpp src/engine/audio_sdl.cpp src/engine/stb_vorbis_impl.cpp src/engine/assets.cpp src/engine/log.cpp src/engine/platform_paths_sdl.cpp" ;;
    viewer) echo "apps/viewer.cpp $ENGINE" ;;
    bobrhopper) echo "apps/bobrhopper.cpp $ENGINE $GAME src/game/scene_render.cpp src/ui/hud.cpp src/ui/debug_overlay.cpp src/ui/screens.cpp src/ui/lang.cpp src/ui/ranks.cpp src/ui/controls.cpp" ;;
    test_hop) echo "tests/test_hop.cpp $LOGIC" ;;
    trace) echo "apps/trace.cpp $LOGIC" ;;
    mapdump) echo "apps/mapdump.cpp $LOGIC" ;;
    # the trace tool without SDL, for the MIPS soft-float build under qemu (build/build_host.sh mipsel-trace)
    trace_nosdl) echo "apps/trace.cpp $GAME src/engine/math.cpp src/engine/assets.cpp src/engine/log.cpp src/engine/gsap.cpp src/sf2000/platform_paths_sf2000.cpp" ;;
    # SF2000 (build/build_sf2000.sh, mips-mti-elf): the game logic and the SDL-free engine
    sf2000_logic) echo "$GAME $ENGINE_FIXED" ;;
    # the core linked into core_87000000 (with the multicore framework objects)
    sf2000_core) echo "src/sf2000/libretro_core.cpp src/sf2000/render_bench.cpp src/sf2000/platform_paths_sf2000.cpp $GAME $ENGINE_FIXED $SW $SW_SCENE $UI" ;;
    # the same core in a libretro frontend for the PC / qemu (build/build_host.sh)
    sf2000_host) echo "apps/sf2000_host.cpp src/sf2000/host_fw.cpp src/engine/png_write.cpp src/sf2000/libretro_core.cpp src/sf2000/render_bench.cpp src/sf2000/platform_paths_sf2000.cpp $GAME $ENGINE_FIXED $SW $SW_SCENE $UI" ;;
    *) return 1 ;;
  esac
}
