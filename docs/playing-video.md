# Playing `video.mjpeg` on the PC

`make capture` records `/sdcard/cap/run_NNN/video.mjpeg` — a raw concatenation
of the sensor's grayscale JPEG frames. There is **no container and no
timestamp**, so a player has to be told the frame rate and most players that
sniff the extension will refuse it. `ffmpeg` handles it directly; everything
below is verified with ffmpeg 8.

## Get ffmpeg

| OS | Command |
|---|---|
| Windows | `winget install Gyan.FFmpeg` (or the build from gyan.dev) |
| macOS | `brew install ffmpeg` |
| Linux | `apt install ffmpeg` |

`ffmpeg` brings `ffplay` and `ffprobe`.

## Commands

`-framerate` must come **before** `-i`. Copy the file off the SD card first.

```bash
# Play it immediately
ffplay -framerate 25 video.mjpeg

# Lossless remux to AVI (MJPG) -> VLC / Windows Media Player / mpv
ffmpeg -framerate 25 -i video.mjpeg -c:v copy video.avi

# Re-encode to MP4 (H.264, small, plays anywhere)
ffmpeg -framerate 25 -i video.mjpeg -pix_fmt gray -c:v libx264 -crf 18 video.mp4

# Count frames (check a recording without playing it)
ffprobe -v error -count_frames -select_streams v:0 \
        -show_entries stream=nb_read_frames -of csv=p=0 -f mjpeg video.mjpeg

# Extract individual frames for analysis (grayscale PNGs)
ffmpeg -framerate 25 -i video.mjpeg frame_%05d.png
```

## Frame rate

The rate is **your choice, not the file's** — nothing in `video.mjpeg` records
it. The capture runs ~24–25 fps (variable); anything close looks right, but it
is not frame-exact. For the measured rate, read the boot log: in
`@@STATS capture_fps_x10=249` the capture ran at 24.9 fps, so use
`-framerate 24.9`.

## Notes

- Do **not** rename the file to `.mp4` — it is raw frames, not a container.
  Convert it (above) or keep the `.mjpeg`/`.mjpg` extension; VLC can often open
  the raw file, other players usually cannot.
- The frames are grayscale, so the decoded video is greyscale regardless of the
  converter's pixel format.
