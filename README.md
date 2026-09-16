# SSX (Android 2013) → PS Vita so-loader skeleton

Loads the **user's own** `libgame.so` (com.ea.ssx v0.0.7833, ARMv7) on Vita via the
so-loader technique (TheOfficialFloW / Rinnegatamante). Ships no game code or assets.

## Build (PC)
- VitaSDK + `vdpm` packages: `vitaGL`, `vitashark`, `SceShaccCg`, `kubridge`, `taihen`, `mathneon`
- `mkdir build && cd build && cmake .. && make`  →  `SSX.vpk`

## Install (Vita — needs `kubridge.skprx` + `libshacccg.suprx`)
```
ux0:data/ssx/lib/libgame.so
ux0:data/ssx/lib/libgnustl_shared.so
ux0:data/ssx/obb/*.big            ← every .big from the APK/OBB (bootcache, memresident, cache0-8, data0, ...)
ux0:data/ssx/obb/<any other asset dirs from the OBB, same layout>
```
`libnimble.so` / `libmercury.so` are NOT loaded — the 4 symbols libgame imports from them are stubbed (tracking off).
Log: `ux0:data/ssx/ssx.log` (rewritten each boot).

## What's implemented
- `default_dynlib.c`: all 425 non-gnustl imports → newlib/VitaGL/stubs (7 `std::string`/rb_tree symbols resolve from the real gnustl module)
- `stubs.c`: fork/exec/network/sched/unwind/Bionic internals neutered; network hard-fails so Nimble/Synergy/Trust5 go offline immediately
- `jni_patch.c`: fake JNIEnv (229-slot table, unknown slots trap loudly), name-based method dispatch for EGL/Storage/Display/Trust5/VideoPlayer
- `main.c`: gnustl→libgame load, Blast boot sequence, touch injection, frame loop

## Iteration loop (step 4)
1. Boot, read `ssx.log`. Last "ok" line = where it died.
2. `JNI: unknown method X` → add `X` to `methods[]` in jni_patch.c with the right return type.
3. `JNI: unimplemented JNIEnv slot` → look up slot index in `jni.h`, implement, `SLOT()` it.
4. Crash in game code → address − 0x98000000 = offset into libgame.so → Ghidra (symbols are intact).
5. Use `so_hook` (in so_util.h) to override game functions by symbol name once identified.

## Known open items, in order of pain
- **Shaders**: game embeds GLSL ES. VitaGL + vitashark translate at runtime; expect ~5–10% of the ~200 unique programs to need hand fixes (precision qualifiers, `#version`, uniform arrays). Dump failures with `vglSetSemanticBinding` logging / shark error callback.
- **Audio**: `EAMAudioCoreWrapper` expects Java to pull PCM via AudioTrack. Find the native mix callback (unstripped: grep `EAAudioCore` `Render`/`Mix` symbols), create a SceAudioOut thread that calls it and pushes 44.1k stereo S16.
- **Memory**: watch `sceKernelGetFreeMemorySize()`; if `.big` cache loads OOM, hook the cache allocator (look for `cache0.big`… string xrefs) and shrink counts/sizes.
- **Input**: touch signature in `main.c` is a guess; confirm arg order in Ghidra. Wire SceCtrl → Android keycodes for a real pad experience.
- **Video**: VP6 replays and MP4 cutscenes stubbed as instant-complete. Later: SceAvPlayer for MP4.
- **Xperia check**: was Java-side; irrelevant here. Trust5 IAP: stubbed "owned".

## Legal
Loader code only. so_util.{c,h}/elf.h © Andy Nguyen, MIT (LICENSE.so_util). You must own the game.
