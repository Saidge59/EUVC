# UVC Device Emulator

This project emulates UVC (USB Video Class) devices in Linux kernel space, allowing you to create virtual video devices that can stream frames from raw image files.

## Prerequisites
- Linux kernel with `videobuf2_vmalloc` and `videobuf2_v4l2` modules
- Root access for loading kernel modules and device management
- VLC or other video player for testing
- FFmpeg for frame conversion

## Installation

1. Load required kernel modules:
   ```bash
   sudo modprobe -a videobuf2_vmalloc videobuf2_v4l2
   ```

2. Insert the UVC module:
   ```bash
   sudo insmod uvc.ko
   ```

## Basic Usage

### Device Management

**List available devices:**
```bash
sudo ./uvc-cli -l -d /dev/uvcctl
```

**Create a new RGB24 device (640x480 @30fps):**
```bash
sudo ./uvc-cli -c --frames-dir ~/develop/kernel/uvc/raw_frames_640x480/ --color-scheme RGB -b 16 -r 640x480 -f 30 -d /dev/uvcctl
```

**Create a new YUYV device (640x480 @30fps, 16bpp):**
```bash
sudo ./uvc-cli -c --frames-dir ~/develop/kernel/uvc/raw_frames_640x480/ --color-scheme YUV -r 640x480 -f 30 -d /dev/uvcctl
```

**Modify an existing device:**
```bash
sudo ./uvc-cli -m 1 --frames-dir ~/develop/kernel/uvc/raw_frames_640x480/ -r 640x480 -f 30 -d /dev/uvcctl
```

### Testing with Video Tools

**Inspect RGB24 device properties:**
```bash
v4l2-ctl -d /dev/video0 --all
```

**View RGB24 stream in VLC:**
```bash
vlc --v4l2-chroma=RV24 v4l2:///dev/video0
```

**Inspect YUYV device properties:**
```bash
v4l2-ctl -d /dev/video1 --all
```

**View YUYV stream in VLC:**
```bash
vlc --v4l2-chroma=YUYV v4l2:///dev/video1
```

## Frame Preparation

**Convert video to RGB24 raw frames (320x240):**
```bash
ffmpeg -i ../test.mp4 -f image2 -pix_fmt rgb24 -vf scale=320:240 output_%04d.raw
```

**Convert video to YUYV raw frames (640x480):**
```bash
ffmpeg -i ../test.mp4 -f image2 -pix_fmt yuyv422 -vf scale=640:480 output_%04d.raw
```

## Command Reference

| Option               | Description                          |
|----------------------|--------------------------------------|
| `-h, --help`         | Show help message                   |
| `-c, --create`       | Create new UVC device               |
| `-m, --modify <idx>` | Modify existing device              |
| `-R, --remove <idx>` | Remove device                       |
| `-l, --list`         | List available devices              |
| `-r, --resolution`   | Set resolution (WIDTHxHEIGHT)       |
| `-f, --fps`          | Set frames per second               |
| `-e, --exposure`     | Set exposure value (-100..100)      |
| `-g, --gain`         | Set gain value (-50..150)           |
| `--color-scheme`     | Set color scheme (RGB/YUV)          |
| `-b, --bpp`          | Set bits per pixel                  |
| `--frames-dir`       | Load frames from directory          |
| `-d, --device`       | Device node (default: /dev/uvcctl)  |
