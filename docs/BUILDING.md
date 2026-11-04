# Building and development

C++17, pinned whisper.cpp v1.7.6, and dependency-free HTML/CSS/Canvas. The shipped stable download is the Windows x64 Vulkan build.

## Native build

Windows presets use **Visual Studio 2026 / MSVC v145**, **Windows SDK 10.0.28000.0** and **CMake 4.2+**. Use a matching Developer PowerShell. Dependencies and exact hashes are pinned in `buildspec.json` and `cmake/whisper.cmake`.

CPU-only source build:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel 4
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
cmake --install build_x64 --config RelWithDebInfo --prefix package
```

Vulkan build, with a compatible Vulkan SDK available for CMake discovery:

```powershell
cmake --preset windows-x64-gpu
cmake --build --preset windows-x64-gpu --parallel 4
ctest --test-dir build_x64_gpu -C RelWithDebInfo --output-on-failure
cmake --install build_x64_gpu --config RelWithDebInfo --prefix package_gpu
```

Models are not required to compile; model-backed tests need the exact supported models. A source CPU-only build avoids Vulkan Runtime. Windows presets target AVX2/FMA/F16C; older CPUs need an adjusted build.

## Tests

For overlay development with Node.js:

```powershell
node --test tests/browser-overlay.test.mjs tests/caption-animation.test.mjs tests/caption-controls.test.mjs
```

The suite covers revisions/ACKs, retention, reading after entrance, hidden/stalled frames, reload, wrapping, bounds and animation behavior. Bounded generated Unicode inputs do not constitute exhaustive coverage.

Native tests cover audio/filter/model/pipeline/rolling plus Windows journal/HTTP and real-libobs JSON protocol. Real-libobs tests need its runtime DLLs on PATH. Stub lifecycle tests are not live OBS verification.

## Reproduce showcase media

With Node.js and Chrome on Windows:

```powershell
node scripts/render-showcase.mjs
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/encode-showcase.ps1
```

Set `CHROME_PATH` for a non-default installation. The capture script serves unchanged production overlay assets with synthetic transcript data, records screenshots/frames and closes its local server/browser. The encoder uses Windows WPF imaging to produce looping GIFs. Frames stay in ignored `.work/`; final assets go to `docs/media/`.

Neither tool is a plugin runtime dependency. Animation captures do not measure speech recognition speed.

## Repository scope

Keep models, builds, dependency downloads, scene backups and machine-specific logs out of Git. Prebuilt packages belong in GitHub Releases; frozen local archives are separate from the source repository.

Optional real-model mute check (local PCM16 mono 16kHz fixture required):

```powershell
.\build_feature\tests\RelWithDebInfo\worker-mute.exe models/ggml-base.bin .work/russian.raw
```

This checks startup mute, cancellation during inference, rapid mute/unmute, fresh speech resumption and a single model load. It does not control a live OBS session.

For a Vulkan-enabled build, append `--gpu` to the worker-mute command to exercise GPU inference and cancellation.
