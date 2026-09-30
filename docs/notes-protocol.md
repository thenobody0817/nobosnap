# `omasnap-notes/1`: directions carried in a PNG

Omasnap (and its Nobosnap fork) can attach free-form directions to a numbered
marker. The directions are written into the least-significant bits of the
exported PNG's RGB channels, so the picture looks untouched while a decoder can
recover "marker 3 says: move this button beside Search" together with the
marker's position in the final image.

This is **hidden storage, not encryption or authentication**. Anyone with the
file and this document can read and alter the payload. Treat it as a comment
embedded in the image, not as a secret or a signature.

## Logical payload

The decoded data is compact JSON:

```json
{
  "protocol": "omasnap-notes/1",
  "image": { "width": 1920, "height": 1080 },
  "notes": [
    {
      "id": "42",
      "number": 3,
      "position": [0.42, 0.28],
      "instruction": "Move this button beside Search.\nKeep the icon."
    }
  ]
}
```

- `position` is normalized to `[0, 1]` over the **final exported image**, with
  the origin at its top-left. It is computed after cropping, backdrop padding,
  canvas growth, and export scaling, so it matches the pixels the reader sees.
- Markers whose position falls outside the exported image are not written.
- `instruction` is full Unicode and may contain newlines.
- `id` is the marker layer's stable identity, used to keep directions attached
  to the right marker across undo/redo and duplication.

## Binary framing

Before the JSON is embedded it is wrapped in a small binary header so a decoder
can tell "no payload" from "corrupt payload". All multi-byte integers are
big-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic `OMAS` |
| 4 | 1 | Version (`1`) |
| 5 | 1 | Compression: `0` raw, `1` zlib (libdeflate) |
| 6 | 4 | Length of the **data** section in bytes |
| 10 | 4 | CRC-32 of bytes 4..9 (version, compression, length) plus the data section |
| 14 | 4 | Uncompressed payload length |
| 18 | .. | Payload bytes (zlib stream when compression = `1`) |

The JSON is compressed with zlib when that actually shrinks it. A decoder caps
both the declared and the uncompressed lengths (16 MiB) and verifies the CRC
before parsing, so a hostile length cannot make it allocate or read out of
bounds.

## How the bits are hidden

The payload is written into the **RGB least-significant bits** of the rendered
image, three bits per pixel, in raster order (left to right, top to bottom),
and within each pixel red, then green, then blue, most-significant bit first.
Alpha is never touched.

Only `ARGB32` and `RGB32` images are written in place. Premultiplied and packed
formats (for example `RGB888`) are converted to `ARGB32` first, because a PNG
save unpremultiplies and repacks them and would discard the embedded bits.

Capacity is checked **before** writing: a payload needs `8 * size` bits and the
image offers `width * height * 3`. A payload that does not fit fails loudly; it
is never truncated silently.

## Reading it back

```bash
omasnap-notes inspect screenshot.png   # human-readable summary
omasnap-notes decode  screenshot.png   # numbered directions
omasnap-notes decode  screenshot.png --json
```

Exit codes distinguish the outcomes: `0` payload found, `1` no payload, `2`
unsupported version, `3` corrupt payload, `4` capacity/decoder limit. A corrupt
or absent payload is never partially reported as valid directions.

## Limitations

- **The bits survive only lossless, pixel-identical re-encoding.** Saving the
  same pixels as a new PNG keeps the payload; cropping, resizing, rotating,
  JPEG conversion, screenshotting the image, or any other pixel-altering step
  destroys it.
- It is invisible but not fragile-by-design: editing tools that touch the
  least-significant bits can corrupt it. The CRC turns that into a clean
  "corrupt payload" rather than wrong directions.
- There is no visible comments panel, QR border, or readable fallback. If a
  reader cannot decode the payload, the directions do not exist for them.
