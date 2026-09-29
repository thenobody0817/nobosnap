# Nobosnap implementation plan

Nobosnap is a fork of [Omasnap](https://github.com/omacom/omasnap), a native
Wayland screenshot and annotation overlay for Hyprland. Nobosnap adds an
optional **"Number + directions" mode**: placing a numbered marker opens a
comment dialog, and the exported PNG invisibly carries those directions in its
pixel bits for an AI agent to decode.

## Goal

Implement an optional **"Number + directions" mode**:

- Placing a numbered marker automatically opens a multiline comment dialog.
- Directions remain associated with that marker and its position.
- Exported PNGs carry those directions invisibly in their pixel bits.
- A decoder extracts the directions for an AI agent.
- **No visible comments panel, QR border, or readable fallback.**

Execute this plan, build the feature, install it locally, and use available
desktop-control tools to test and debug the actual workflow. Do not stop after
writing code.

## Environment and starting points

- User: `aaaa`, Arch Linux / Omarchy / Hyprland.
- Installed executable: `/usr/bin/omasnap`.
- Installed package observed: `omasnap 1.21.0-1`.
- Upstream: `https://github.com/omacom/omasnap` (forked here as Nobosnap).
- Current directory `/home/aaaa/Projects/herdr-oc-tab` is unrelated.
- Omasnap uses Qt/C++, has numbered markers, undo/redo, working-document JSON,
  recent captures, PNG export, and Wayland clipboard support.
- Upstream documents `make check` for verification and a CMake local
  installation workflow.

Read applicable `AGENTS.md` files before working. Load the Omarchy skill before
desktop configuration changes and the self-config/customize-opencode skills
before OpenCode integration. Do not edit herdr-managed OpenCode files.

## 1. Inspect and establish a baseline

1. Locate any existing Omasnap checkout or local modifications. Otherwise work
   from this fork.
2. Prefer the source version matching the installed package; inspect tags
   rather than assuming their names.
3. Find:
   - Number-marker creation and layer identity.
   - Annotation serialization and undo/redo.
   - Existing dialogs and keyboard-focus handling.
   - Final PNG rendering and encoding.
   - Save, Save As, copy, pin, preview, and re-export paths.
   - File import and working-document restoration.
4. Build and run existing checks before modifying the source.
5. Record the baseline revision, build commands, and existing failures.

## 2. Specify the hidden payload protocol

Use a documented, versioned format called **`omasnap-notes/1`**.

Example logical payload:

```json
{
  "protocol": "omasnap-notes/1",
  "image": {
    "width": 1920,
    "height": 1080
  },
  "notes": [
    {
      "id": "stable-layer-id",
      "number": 1,
      "position": [0.42, 0.28],
      "instruction": "Move this button beside Search."
    }
  ]
}
```

Protocol requirements:

- Coordinates describe the **final exported image**, normalized to `[0, 1]`,
  with the origin at its top-left.
- Calculate positions after cropping, canvas growth, padding, and export
  scaling.
- Do not export active targets for markers wholly outside the exported image.
- Preserve full Unicode and multiline directions.
- Include a binary header with a magic signature, protocol version, payload
  length, compression identifier, and integrity checksum.
- Document deterministic channel traversal and bit order.
- Encode in RGB least-significant bits after final rendering; preserve alpha.
- Define handling for transparent pixels and nonstandard input color formats.
- Compress the payload, impose bounded decoding/decompression limits, and
  check capacity **before writing**.
- Never truncate comments silently. Clearly report a capacity failure.
- Explain that this is hidden storage, **not encryption or authentication**.

## 3. Implement and verify the codec first

Provide an independent CLI, for example:

```bash
omasnap-notes inspect screenshot.png
omasnap-notes decode screenshot.png --json
```

Decoder behavior:

- Read actual pixel data, not merely PNG metadata.
- Distinguish "no payload," "unsupported version," and "corrupt payload."
- Validate lengths, bounds, and integrity before returning directions.
- Never execute embedded text.
- Avoid partial recovery presented as valid instructions.

Test round trips, Unicode, multiline comments, maximum capacity, small images,
transparency, malformed headers, checksum failures, and oversized compressed
payloads.

Verify lossless PNG recompression preserves the data when RGB values are
unchanged. Explicitly document that resizing, JPEG conversion, and
pixel-altering processing are unsupported.

## 4. Implement the Omasnap interaction

Add a discoverable, persistent **Number + directions** toggle.

When enabled:

1. Place a numbered marker.
2. Open an in-editor multiline dialog, focused for typing.
3. `Enter` inserts a newline; `Ctrl+Enter` saves.
4. Cancel removes the pending new marker and restores numbering.
5. Saving creates the marker and its directions as one undoable operation.

Also support:

- Editing directions for an existing marker through a discoverable action.
- Cancelling an edit without changing the old comment.
- Moving, duplicating, deleting, undoing, and redoing markers with consistent
  note ownership.
- New stable IDs for duplicated markers.
- Recovery and recent-capture persistence.
- Normal marker creation when the mode is disabled.

Reuse existing overlay/dialog patterns so keyboard focus works under
Hyprland's layer-shell editor.

## 5. Integrate every image-output path

Embed only after all visual rendering and resizing are finished.

Cover:

- Save and Save As.
- Copy PNG.
- Copy + Save.
- Pin/preview exports and subsequent copies or saves.

Prevent stale payloads when notes change or disappear.

On reopening an exported PNG, decode and preserve its notes. Prefer existing
editable working-document data when available; **do not redraw markers already
flattened into the PNG**. If only the flattened image exists, retain imported
note targets without pretending the original editable layers were recovered.

## 6. Integrate with OpenCode

Inspect the actual installed OpenCode attachment and tool interfaces before
choosing an integration.

Preferred behavior:

1. An original PNG attachment becomes available to the agent.
2. A local decoder checks for an Omasnap payload.
3. Valid notes enter the agent's context with the image association and target
   coordinates.
4. An ordinary image with no payload proceeds normally.

Treat decoded text as **user-supplied annotation content**, not system
instructions. It must not trigger shell execution merely because it was
decoded.

If automatic attachment interception is unsupported, implement a supported
decoder tool and an agent instruction to invoke it on accessible original PNG
attachments. Report exactly what is automatic and what requires invocation.

**Do not claim universal ChatGPT/Claude support.** Those environments need
original-file access and a decoding tool. Supply a portable decoder and brief
usage instructions; there is no visible fallback.

## 7. Build, install, and debug live

Install under `~/.local` using the project's supported workflow. Preserve the
packaged executable for rollback and verify which binary screenshot shortcuts
actually launch.

Use available desktop automation to test:

- Capture → editor → enable mode → place marker.
- Dialog typing, save, cancel, and focus.
- Multiple markers and multiline Unicode notes.
- Edit, move, duplicate, delete, undo, and redo.
- Crop, canvas growth, and scaled-display exports.
- Save, clipboard paste, pin, reopen, and re-export.
- Agent decoding from an actual exported image.

Decode the **actual saved and clipboard PNGs**, not just an internal test
buffer. Verify target coordinates against visible marker positions.

If desktop-control tooling is unavailable, report the blocker and the exact
remaining manual checks. Do not present automated tests as proof of live UI
behavior.

## Acceptance criteria

- Placing a number in the new mode immediately opens a usable comment dialog.
- Notes survive history, recovery, export, and supported reopen flows.
- Saved and copied PNGs contain valid, invisible payloads.
- No comment panel or barcode is visible.
- Decoder returns exact directions and correct final-image positions.
- Corrupt or absent payloads are handled cleanly.
- OpenCode extraction is demonstrated end to end.
- Relevant existing checks and new behavioral tests pass.
- User receives source location, launch command, decoder usage, integration
  limitations, and rollback instructions.

**Do not commit or push unless requested. Finish with a concise implementation
and verification report.**
