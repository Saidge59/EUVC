# Virtual Camera Device Driver for Linux

This Linux module implements a simplified virtual V4L2 compatible camera device driver with raw framebuffer input.

## Prerequisite

The following packages must be installed before building `uvc`.

In order to compile the kernel driver successfully, package versions of the currently used kernel, kernel-devel, and kernel-headers need to be matched:
```shell
sudo apt install linux-headers-$(uname -r)
```

Since `uvc` is built with [V4L2](https://en.wikipedia.org/wiki/Video4Linux) (Video4Linux, second version), `v4l-utils` is necessary for retrieving more information and function validation:
```shell
sudo apt install v4l-utils
```

## Build and Run

After running `make`, you should be able to generate the following files:
- `uvc.ko` - Linux kernel module.
- `uvc-cli` - Sample utility to configure virtual camera device(s).

### Initialization
Before loading the kernel module, you need to satisfy its dependencies and initialize the modules:
```shell
sudo modprobe -a videobuf2_vmalloc videobuf2_v4l2
sudo insmod uvc.ko
```

Alternatively, use the `uvc-cli` utility to initialize modules automatically:
```shell
sudo ./uvc-cli -i
```

Expectedly, device nodes will be created in `/dev`:
- `videoX` - V4L2 device (e.g., `/dev/video0`, `/dev/video1`).
- `uvcctl` - Control device for virtual camera(s), used by `uvc-cli`.

### Verification
Run the following command to check if the driver is functioning correctly:
```shell
sudo v4l2-compliance -d /dev/video0 -f
```
It should return 0 failed and 0 warnings at the end.

To check configured formats and emulated controls:
```shell
sudo v4l2-ctl -d /dev/video0 --all
```
Example output:
```
Driver Info:
    Driver name   : uvc
    Card type     : uvc
    Bus info      : platform: virtual
    Driver version: 4.15.18
    Capabilities  : 0x85200001
        Video Capture
        Read/Write
        Streaming
        Extended Pix Format
        Device Capabilities
```

### Module Parameters
Available parameters for `uvc` kernel module:
- `devices_max` - Maximal number of devices (default: 8).
- `create_devices` - Number of devices to be created during initialization (default: 1).

### Usage of uvc-cli
Run `uvc-cli --help` for a full list of options. Parameters are persistent after driver load and can be set incrementally. Below are examples for each option:

- **`-h, --help`**: Display help message.
  ```shell
  sudo ./uvc-cli -h
  ```

- **`-i, --init`**: Initialize modules (load `videobuf2_vmalloc`, `videobuf2_v4l2`, and `uvc.ko`).
  ```shell
  sudo ./uvc-cli -i
  ```
  *Output*: "Modules loaded successfully."

- **`-D, --deinit`**: Deinitialize modules (unload `uvc.ko` and `videobuf2` modules).
  ```shell
  sudo ./uvc-cli -D
  ```
  *Output*: "Modules deinitialized successfully."

- **`-c, --create`**: Create a new emulated UVC device.
  ```shell
  sudo ./uvc-cli -c
  ```
  *Creates a new device with default settings (e.g., 800x700, GRAY8).*

- **`-m, --modify <idx>`**: Modify an existing device (e.g., index 1).
  ```shell
  sudo ./uvc-cli -m 1 -r 640x480
  ```
  *Modifies device 1 to 640x480 resolution (previous settings like FPS persist).*

- **`-R, --remove <idx>`**: Remove a device (emulate unplug).
  ```shell
  sudo ./uvc-cli -R 1
  ```
  *Removes device at index 1.*

- **`-l, --list`**: List all devices.
  ```shell
  sudo ./uvc-cli -l
  ```
  *Example output*: 
  ```
  Available virtual UVC compatible devices:
  1. (800,700,gray8,fps=29,exp=100,gain=50,bpp=8) -> /dev/video0
  2. (640,360,gray8,fps=29,exp=100,gain=50,bpp=8) -> /dev/video1
  ```

- **`-r, --resolution <width>x<height>`**: Set resolution.
  ```shell
  sudo ./uvc-cli -m 1 -r 800x700
  ```
  *Sets resolution to 800x700 for device 1 (previous FPS and color scheme persist).*

- **`-C, --crop-ratio <num/den>`**: Set crop ratio.
  ```shell
  sudo ./uvc-cli -m 1 -C 4/3
  ```
  *Applies a 4:3 crop ratio to device 1.*

- **`-f, --fps <fps>`**: Set frames per second.
  ```shell
  sudo ./uvc-cli -m 1 -f 30
  ```
  *Sets FPS to 30 for device 1.*

- **`-e, --exposure <val>`**: Set exposure.
  ```shell
  sudo ./uvc-cli -m 1 -e 100
  ```
  *Sets exposure to 100 for device 1.*

- **`-g, --gain <val>`**: Set gain.
  ```shell
  sudo ./uvc-cli -m 1 -g 50
  ```
  *Sets gain to 50 for device 1.*

- **`--color-scheme <scheme>`**: Set color scheme (RGB, GRAY8).
  ```shell
  sudo ./uvc-cli -m 1 --color-scheme RGB
  ```
  *Sets color scheme to RGB for device 1.*

- **`-b, --bpp <bits>`**: Set bits per pixel (8bpp, 24bpp).
  ```shell
  sudo ./uvc-cli -m 1 -b 24
  ```
  *Sets bits per pixel to 24 for device 1.*

- **`--frames-dir <path>`**: Load frames from directory.
  ```shell
  sudo ./uvc-cli -m 1 --frames-dir /path/to/frames
  ```
  *Loads frames from the specified directory for device 1.*

- **`-d, --device /dev/*`**: Specify device node (default: /dev/uvcctl).
  ```shell
  sudo ./uvc-cli -m 1 -d /dev/uvcctl -r 640x480
  ```
  *Uses /dev/uvcctl as the control device (default behavior).*

- **`-L, --loop <0|1>`**: Enable (1) or disable (0) looping.
  ```shell
  sudo ./uvc-cli -m 1 -L 1
  ```
  *Enables looping for device 1.*

## Viewing the Virtual Camera Stream
You can use VLC to view the video stream sent to the virtual camera via the driver:
```shell
vlc v4l2:///dev/video0
```
This command opens the /dev/video0 device, which is associated with the virtual camera driver, and displays the incoming frames in real time.

To install VLC on Ubuntu, run:
```shell
sudo apt install vlc
```

### Notes
- Parameters are persistent after driver load and can be modified incrementally. For example, after setting `-r 800x700`, subsequent `-m 1 -f 30` will retain the resolution.
- Always run `uvc-cli` with `sudo` due to required root privileges.
- Use `v4l2-ctl` or other V4L2 tools to test the virtual camera with applications like VLC.
- The frames must have names (output_0001.raw, output_0002.raw, ...).

## Cleanup
To unload the module and clean up:
```shell
sudo ./uvc-cli -D
```
