# Windows installation and setup

For **stable-2026-11-04**. No development environment or separate model server is needed.

## Prerequisites

Windows 10/11 x64, an AVX2/FMA/F16C-capable CPU, OBS Studio x64 with Browser Source, and a current Microsoft Visual C++ v14 Redistributable x64. **OBS 32.2.2** is the tested version; newer versions require compatibility checks.

Official downloads: [OBS](https://obsproject.com/download), [tested OBS release](https://github.com/obsproject/obs-studio/releases/tag/32.2.2), [Microsoft runtime documentation](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/) and [x64 runtime installer](https://aka.ms/vc14/vc_redist.x64.exe).

**The Vulkan package links to `vulkan-1.dll` even in CPU mode.** A compatible graphics driver normally supplies this system runtime. Disabling GPU does not remove that dependency. A PC without it needs a separate CPU-only build; do not download individual DLLs from third-party sites. See [building](BUILDING.md).

GPU acceleration was tested on RTX 4070 SUPER, 12 GB. Other devices need validation. The implementation only accepts its selected **Vulkan0** device, so multi-GPU laptops may report GPU unavailable. CPU mode with base is useful for an initial test when the runtime is present.

## Install the plugin

1. Download the Windows ZIP and `caption-styles-sha256.json` from [Releases](https://github.com/esnqduke/OBS-Live-Translate/releases/tag/stable-2026-11-04).
2. Close OBS. Back up an existing plugin folder and export important scene collections before updating.
3. Extract the ZIP; copy the **entire** `obs-live-translate` folder into `C:\ProgramData\obs-studio\plugins\`.
4. Check the resulting layout:

   ```text
   C:\ProgramData\obs-studio\plugins\obs-live-translate\
   ├── bin\64bit\obs-live-translate.dll
   └── data\
       ├── locale\en-US.ini
       ├── locale\ru-RU.ini
       ├── licenses\...
       └── overlay\
           ├── index.html
           ├── overlay.css
           ├── overlay.js
           └── model.mjs
   ```

5. Avoid double nesting or duplicate DLLs in older OBS plugin directories.
6. Reopen OBS. **Live Translate — local Whisper (RU / EN)** should appear under audio filters.

ProgramData is hidden by default; paste the path into Explorer. Windows may request administrator permission to copy files. For portable OBS use its `data\plugins\obs-live-translate\` layout instead; see the [official plugin guide](https://obsproject.com/kb/plugins-guide).

Verify an ordinary installation from the folder containing the release manifest:

```powershell
$ErrorActionPreference = 'Stop'
$pluginPath = Join-Path $env:PROGRAMDATA 'obs-studio/plugins/obs-live-translate'
$manifest = Get-Content -LiteralPath '.\caption-styles-sha256.json' -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
    $file = Join-Path $pluginPath $entry.path
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "File checksum mismatch: $($entry.path)"
    }
}
Write-Host 'Installed plugin files verified.'
```

Adjust `$pluginPath` for portable OBS. Use the manifest from the same release ZIP.

## Models

Download one model or transfer an already verified copy. Models are not included. Save them permanently, for example in `C:\OBS-Models\`.

| Model | Pinned download | Exact bytes |
|---|---|---:|
| base | [ggml-base.bin](https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-base.bin) | 147951465 |
| medium-q5_0 | [ggml-medium-q5_0.bin](https://huggingface.co/ggerganov/whisper.cpp/resolve/c521a4b02f422512d734391fdf08bb08c0862f68/ggml-medium-q5_0.bin) | 539212467 |
| large-v3 | [ggml-large-v3.bin](https://huggingface.co/ggerganov/whisper.cpp/resolve/c521a4b02f422512d734391fdf08bb08c0862f68/ggml-large-v3.bin) | 3095033483 |

SHA256:

```text
ggml-base.bin
60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe

ggml-medium-q5_0.bin
19fea4b380c3a618ec4723c3eef2eb785ffba0d0538cf43f8f235e7b3b34220f

ggml-large-v3.bin
64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2
```

```powershell
$modelPath = 'C:\OBS-Models\ggml-medium-q5_0.bin'
(Get-Item -LiteralPath $modelPath).Length
Get-FileHash -LiteralPath $modelPath -Algorithm SHA256
```

Start with **base** for a lighter initial test, or **medium-q5_0** for the main tested GPU configuration. Large-v3 uses more resources and did not improve the tested translation sample. Only these exact files are accepted; arbitrary quantizations, `.en`, GGUF, ONNX, PyTorch and Turbo do not work.

## Audio and browser setup

1. Select the microphone/audio source to transcribe and confirm its OBS meter responds.
2. Open its **Filters → Audio Filters → + → Live Translate — local Whisper (RU / EN)**. Avoid duplicate filters on copies of the same microphone.
3. Add a **Browser Source** to your scene. For a 1920×1080 canvas use **1920×384** initially. Leave **Local file disabled**; do not select an HTML file manually.
4. In the audio filter choose the model and Browser Source. Refresh the source list if needed, then click **Connect selected browser**.
5. Enable **Translate Russian speech to English** for English, or disable it for Russian transcription. Input language is currently fixed to Russian.
6. Choose GPU or CPU. CPU threads default to 2 and can be set from 1 to 32; leave resources for OBS and your game.
7. Wait for loading and GPU warmup. Refresh status or reopen properties if necessary. Speak Russian, then pause about a second.
8. Position the Browser Source above opaque video sources in the scene order.

The enabled filter loads the model automatically. No server command, Python, Node.js, CUDA Toolkit or API key is needed. The plugin generates the local page URL: do not copy it between PCs or open router ports. If the model moves, update its path.

## Appearance and reading behavior

Choose style and background **in the audio filter**. Manual background settings enable colors, opacity, padding, corners, borders and image fit. PNG/JPEG/WebP files up to 16 MiB are supported. Text style and background are independent.

Styles: Plain, Matrix, Console, Cyberpunk, Matrix Rain, Fantasy and Grand Cathay-inspired. Preview does not interrupt pending real captions. Appearance changes preserve the queue and do not reload the model.

New rows enter below, older rows move up, and each row gets at least three seconds fully readable after entrance. Longer lines stay longer. Hidden/stalled frames pause reading. Fast continuous speech can build a queue; this is intentional. After updating overlay files, use **Refresh cache of current page** in Browser properties. An unfinished phrase may replay after reload.

Text (GDI+) is also supported. Select it instead and configure its font/extents in OBS. New browser effects and decorative backgrounds require Browser Source. Do not enable reading from a text file on the selected GDI+ output.

## Troubleshooting

| Symptom | Check |
|---|---|
| Filter missing or DLL will not load | x64 OBS/runtime, folder structure, duplicate DLLs, CPU instructions, Vulkan Runtime and OBS load log |
| Model rejected | Exact size/SHA256; ensure the download is a binary, not an HTML page |
| GPU unavailable/failed | Driver, Vulkan0 restriction and free VRAM; try base with GPU disabled |
| Preview works but speech does not | Correct audio source, input meter, enabled filter, completed warmup and Russian speech; very quiet input below about −40 dBFS may be gated |
| Browser empty | Output selected/connected, Local file off, complete overlay assets, visible source above video |
| Old animation | Refresh Browser Source page cache |
| Text outside canvas | Browser dimensions, scene resolution, scale, crop and position |
| Delay grows | Allow reading time; reduce inference load/model size if processing also falls behind |
| Queue overflow | Reduce load, let pending captions finish, then disable/enable the filter; a restart clears that session |
| Renamed source or edited URL | Refresh list, reselect output and connect again |

Use **Help → Log Files → View Current Log**. Search for `obs-live-translate`, `ModelInvalid`, `GpuUnavailable`, `GpuInitFailed`, `RequestedGpu`, `using Vulkan`, `BrowserStartFailed` and `CaptionBacklogFull`.

Do not disable antivirus/firewall as a generic fix. If policy blocks the local bridge, inspect its security log and use a narrowly scoped rule for the trusted installation.

## Transfer and rollback

Transfer the release ZIP, matching manifest and verified model. Recreate/import scenes, then reselect microphone, model path, background image and output on the new PC. Device IDs and session URLs are not portable as-is.

For rollback, close OBS, back up the installed plugin and restore the previous release's full folder. Check its manifest before reopening OBS. Scene backups and models are separate from plugin releases.
