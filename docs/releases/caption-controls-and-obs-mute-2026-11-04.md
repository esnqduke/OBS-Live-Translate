# Caption controls and OBS microphone mute — 2026-11-04

OBS Live Translate gives you direct control over subtitle pacing and presentation, and follows the microphone source's mute state in OBS.

## Subtitle display controls

- Set minimum reading time (100–60,000 ms), extra time per character (0–500 ms), minimum row interval (0–30,000 ms), and one to three visible rows.
- Adjust animation speed from 10% to 500%, or disable entrance and scrolling animations.
- Choose automatic font sizing or a manual size from 12 to 200 pixels, and select automatic, left, center or right alignment.
- Show or hide partial recognition results.
- Clear displayed captions, pending captions and buffered audio with one button.
- Restore display defaults while keeping the selected model, style and background.

Reading time starts after a row has fully appeared. Set per-character timing to zero for fixed timing. Changing display settings preserves unread captions and keeps Whisper loaded. Browser Source supports all seven visual styles; Text (GDI+) retains its native effects. Background controls apply to Browser Source.

## OBS microphone mute

Muting the source carrying the filter in the OBS mixer, including via hotkeys, stops audio from entering STT, cancels recognition in progress, and clears displayed and queued captions. Late results from cancelled speech are discarded. Unmuting resumes with new speech and keeps the model loaded.

This follows OBS source mute. Hardware mute and setting the source volume to zero do not set that state.

## Reliability and validation

UI updates support OBS executing callbacks immediately, including during startup with an already muted microphone. Caption tasks are dispatched after releasing the mailbox lock.

- Eight native test suites and 28 JavaScript tests pass.
- Regression coverage includes immediate UI dispatch, muted startup, cancellation, stale results and caption clearing.
- Live startup verified on OBS 32.2.2 with the configured scene and the medium model using Vulkan.

## Installation

Close OBS and update the DLL and the complete data folder together. Refresh the Browser Source page cache after updating overlay files. Model downloads and system requirements are unchanged; see [Windows setup](../INSTALL-WINDOWS.md).

The earlier stable release remains available as a separate download for rollback.
