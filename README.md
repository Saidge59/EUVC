# Virtual Camera Device Driver for Linux

This Linux module implements a simplified virtual V4L2 compatible camera device driver with raw framebuffer input.

## Prerequisite

The following packages must be installed before building `euvc`.

### Required Packages
- **Kernel headers and development tools**:
  - `linux-headers-$(uname -r)`: Must match the currently running kernel version.
  - `gcc-12`: Compiler required for building the module and utility (version 12 or higher).
  - `make`: Build tool for compiling the source code.
  - `libc6-dev`: C library development files.
- **V4L2 dependencies**:
  - `v4l-utils`: Tools for V4L2 compliance testing and validation.
- **Other utilities**:
  - `vlc`: For viewing the virtual camera stream (optional, for testing).

## Build and Run

After running `make`, you should be able to generate the following files:
- `euvc.ko` - Linux kernel module.
- `euvc-cli` - Sample utility to configure virtual camera device(s).

### Initialization
Before loading the kernel module, you need to satisfy its dependencies and initialize the modules:
```shell
sudo modprobe -a videobuf2_vmalloc videobuf2_v4l2
sudo insmod euvc.ko
```

Alternatively, use the `euvc-cli` utility to initialize modules automatically:
```shell
sudo ./euvc-cli -i
```

Expectedly, device nodes will be created in `/dev`:
- `videoX` - V4L2 device (e.g., `/dev/video0`, `/dev/video1`).
- `euvcctl` - Control device for virtual camera(s), used by `euvc-cli`.

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
    Driver name   : euvc
    Card type     : euvc
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
Available parameters for `euvc` kernel module:
- `devices_max` - Maximal number of devices (default: 8).
- `create_devices` - Number of devices to be created during initialization (default: 1).

### Usage of euvc-cli
Run `euvc-cli --help` for a full list of options. Parameters are persistent after driver load and can be set incrementally. Below are examples for each option:

- **`-h, --help`**: Display help message.
  ```shell
  sudo ./euvc-cli -h
  ```

- **`-i, --init`**: Initialize modules (load `videobuf2_vmalloc`, `videobuf2_v4l2`, and `euvc.ko`).
  ```shell
  sudo ./euvc-cli -i
  ```
  *Output*: "Modules loaded successfully."

- **`-D, --deinit`**: Deinitialize modules (unload `euvc.ko` and `videobuf2` modules).
  ```shell
  sudo ./euvc-cli -D
  ```
  *Output*: "Modules deinitialized successfully."

- **`-c, --create`**: Create a new emulated euvc device.
  ```shell
  sudo ./euvc-cli -c
  ```
  *Creates a new device with default settings (e.g., 800x700, GRAY8).*

- **`-m, --modify <idx>`**: Modify an existing device (e.g., index 1).
  ```shell
  sudo ./euvc-cli -m 1 -r 640x480
  ```
  *Modifies device 1 to 640x480 resolution (previous settings like FPS persist).*

- **`-R, --remove <idx>`**: Remove a device (emulate unplug).
  ```shell
  sudo ./euvc-cli -R 1
  ```
  *Removes device at index 1.*

- **`-l, --list`**: List all devices.
  ```shell
  sudo ./euvc-cli -l
  ```
  *Example output*: 
  ```
  Available virtual euvc compatible devices:
  1. (800,700,gray8,fps=29,exp=100,gain=50,bpp=8) -> /dev/video0
  2. (640,360,gray8,fps=29,exp=100,gain=50,bpp=8) -> /dev/video1
  ```

- **`-r, --resolution <width>x<height>`**: Set resolution.
  ```shell
  sudo ./euvc-cli -m 1 -r 800x700
  ```
  *Sets resolution to 800x700 for device 1 (previous FPS and color scheme persist).*

- **`-C, --crop-ratio <num/den>`**: Set crop ratio.
  ```shell
  sudo ./euvc-cli -m 1 -C 4/3
  ```
  *Applies a 4:3 crop ratio to device 1.*

- **`-f, --fps <fps>`**: Set frames per second.
  ```shell
  sudo ./euvc-cli -m 1 -f 30
  ```
  *Sets FPS to 30 for device 1.*

- **`-e, --exposure <val>`**: Set exposure.
  ```shell
  sudo ./euvc-cli -m 1 -e 100
  ```
  *Sets exposure to 100 for device 1.*

- **`-g, --gain <val>`**: Set gain.
  ```shell
  sudo ./euvc-cli -m 1 -g 50
  ```
  *Sets gain to 50 for device 1.*

- **`--color-scheme <scheme>`**: Set color scheme (RGB, GRAY8).
  ```shell
  sudo ./euvc-cli -m 1 --color-scheme RGB
  ```
  *Sets color scheme to RGB for device 1.*

- **`-b, --bpp <bits>`**: Set bits per pixel (8bpp, 24bpp).
  ```shell
  sudo ./euvc-cli -m 1 -b 24
  ```
  *Sets bits per pixel to 24 for device 1.*

- **`--frames-dir <path>`**: Load frames from directory.
  ```shell
  sudo ./euvc-cli -m 1 --frames-dir /path/to/frames
  ```
  *Loads frames from the specified directory for device 1.*

- **`-d, --device /dev/*`**: Specify device node (default: /dev/euvcctl).
  ```shell
  sudo ./euvc-cli -m 1 -d /dev/euvcctl -r 640x480
  ```
  *Uses /dev/euvcctl as the control device (default behavior).*

- **`-L, --loop <0|1>`**: Enable (1) or disable (0) looping.
  ```shell
  sudo ./euvc-cli -m 1 -L 1
  ```
  *Enables looping for device 1.*

## Viewing the Virtual Camera Stream
You can use VLC to view the video stream sent to the virtual camera via the driver:
```shell
vlc v4l2:///dev/video0
```
This command opens the /dev/video0 device, which is associated with the virtual camera driver, and displays the incoming frames in real time.

### Notes
- Parameters are persistent after driver load and can be modified incrementally. For example, after setting `-r 800x700`, subsequent `-m 1 -f 30` will retain the resolution.
- Always run `euvc-cli` with `sudo` due to required root privileges.
- Use `v4l2-ctl` or other V4L2 tools to test the virtual camera with applications like VLC.
- The frames must have names (Frame_001.raw, Frame_0002.raw, ...).

## Cleanup
To unload the module and clean up:
```shell
sudo ./euvc-cli -D
```
