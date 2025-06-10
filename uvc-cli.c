#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "uvc.h"

static const char *short_options = "hcm:r:l:d:f:e:g:R:cs:b:fd:";

const struct option long_options[] = {
    {"help", 0, NULL, 'h'},
    {"create", 0, NULL, 'c'},
    {"modify", 1, NULL, 'm'},
    {"list", 0, NULL, 'l'},
    {"device", 1, NULL, 'd'},
    {"remove", 1, NULL, 'r'},
    {"fps", 1, NULL, 'f'},
    {"exposure", 1, NULL, 'e'},
    {"gain", 1, NULL, 'g'},
    {"resolution", 1, NULL, 'R'},
    {"color-scheme", 1, NULL, 0x100},
    {"bpp", 1, NULL, 'b'},
    {"frames-dir", 1, NULL, 0x101},
    {NULL, 0, NULL, 0}
};

const char *help =
    "\n"
    "**********************************************************************\n"
    "**                     UVC Device Management Help                   **\n"
    "**********************************************************************\n"
    "\n"
    "  -h, --help                        Display this help message         \n"
    "  -c, --create                      Create new emulated UVC device    \n"
    "  -m, --modify <idx>                Modify existing device            \n"
    "  -R, --remove <idx>                Remove a device                   \n"
    "  -l, --list                        List all devices                  \n"
    "  -r, --resolution <width>x<height> Set resolution (e.g. 640x480)     \n"
    "  -f, --fps <fps>                   Set frames per second             \n"
    "  -e, --exposure <val>              Set exposure (-100..100)          \n"
    "  -g, --gain <val>                  Set gain (-50..150)               \n"
    "  --color-scheme <scheme>           Set color scheme (RGB, YUV)       \n"
    "  -b, --bpp <bits>                  Set bits per pixel                \n"
    "  --frames-dir <path>               Load frames from directory        \n"
    "  -d, --device /dev/*               Device node (default: /dev/uvcctl)\n\n";

enum ACTION { ACTION_NONE, ACTION_CREATE, ACTION_DESTROY, ACTION_MODIFY };

struct uvc_device_spec device_template = {
    .width = 640,
    .height = 480,
    .color_scheme = UVC_COLOR_RGB,
    .video_node = "",
    .fb_node = "",
    .fps = 30,
    .exposure = 100,
    .gain = 50,
    .bits_per_pixel = 24
};

static char ctl_path[128] = "/dev/uvcctl";

static bool parse_cropratio(char *res_str, struct uvc_device_spec *dev)
{
    struct crop_ratio cropratio;
    char *tmp = strtok(res_str, "/:,");
    if (!tmp)
        return false;

    cropratio.numerator = atoi(tmp);
    tmp = strtok(NULL, "/:,");
    if (!tmp)
        return false;
    cropratio.denominator = atoi(tmp);

    if (cropratio.numerator > cropratio.denominator || cropratio.denominator == 0)
        return false;
    dev->cropratio = cropratio;
    return true;
}

bool parse_resolution(char *res_str, struct uvc_device_spec *dev)
{
    char *tmp = strtok(res_str, "x:,");
    if (!tmp)
        return false;
    dev->width = atoi(tmp);
    tmp = strtok(NULL, "x:,");
    if (!tmp)
        return false;
    dev->height = atoi(tmp);

    tmp = strtok(NULL, "x:,");
    if (tmp) {
        return parse_cropratio(tmp, dev);
    }
    return true;
}

int load_frames_from_dir(const char *dir_path, struct uvc_device_spec *dev_spec)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if (!(dir = opendir(dir_path))) {
        perror("Failed to open directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "output_") == entry->d_name) {
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        fprintf(stderr, "No output_*.raw files found in %s\n", dir_path);
        return -1;
    }

    dev_spec->frame_count = count;
    strncpy(dev_spec->frames_dir, dir_path, sizeof(dev_spec->frames_dir) - 1);
    dev_spec->frames_dir[sizeof(dev_spec->frames_dir) - 1] = '\0';

    printf("Loaded %d frames from %s\n", count, dir_path);
    return 0;
}

int create_device(struct uvc_device_spec *dev)
{
    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    if (!dev->width || !dev->height) {
        dev->width = device_template.width;
        dev->height = device_template.height;
    }

    if (!dev->color_scheme)
        dev->color_scheme = device_template.color_scheme;

    if (!dev->fps)
        dev->fps = device_template.fps;
    if (!dev->exposure)
        dev->exposure = device_template.exposure;
    if (!dev->gain)
        dev->gain = device_template.gain;
    if (!dev->bits_per_pixel)
        dev->bits_per_pixel = device_template.bits_per_pixel;

    int res = ioctl(fd, UVC_IOCTL_CREATE_DEVICE, dev);
    if (res) {
        fprintf(stderr, "Failed to create a new device.\n");
    } else if (dev->frames_dir[0]) {
        printf("Loading frames from %s\n", dev->frames_dir);
    }

    close(fd);
    return res;
}

int remove_device(struct uvc_device_spec *dev)
{
    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    int res = ioctl(fd, UVC_IOCTL_DESTROY_DEVICE, dev);
    if (res) {
        fprintf(stderr, "Failed to remove the device on index %d.\n",
                dev->idx + 1);
    }

    close(fd);
    return res;
}

int modify_device(struct uvc_device_spec *dev)
{
    struct uvc_device_spec orig_dev = {.idx = dev->idx};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    if (ioctl(fd, UVC_IOCTL_GET_DEVICE, &orig_dev)) {
        fprintf(stderr, "Failed to find device on index %d.\n",
                orig_dev.idx + 1);
        close(fd);
        return -1;
    }

    if (!dev->width || !dev->height) {
        dev->width = orig_dev.width;
        dev->height = orig_dev.height;
    }

    if (!dev->color_scheme)
        dev->color_scheme = orig_dev.color_scheme;

    if (!dev->fps)
        dev->fps = orig_dev.fps;
    if (!dev->exposure)
        dev->exposure = orig_dev.exposure;
    if (!dev->gain)
        dev->gain = orig_dev.gain;
    if (!dev->bits_per_pixel)
        dev->bits_per_pixel = orig_dev.bits_per_pixel;
    if (!dev->cropratio.numerator || !dev->cropratio.denominator)
        dev->cropratio = orig_dev.cropratio;

    int res = ioctl(fd, UVC_IOCTL_MODIFY_SETTING, dev);
    if (res) {
        fprintf(stderr, "Failed to modify the device.\n");
    } else if (dev->frames_dir[0]) {
        printf("Updating frames from %s\n", dev->frames_dir);
    }

    close(fd);
    return res;
}

int list_devices()
{
    struct uvc_device_spec dev = {.idx = 0};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    printf("Available virtual UVC compatible devices:\n");
    while (!ioctl(fd, UVC_IOCTL_GET_DEVICE, &dev)) {
        dev.idx++;
        printf("%d. %s(%d,%d,%s,fps=%d,exp=%d,gain=%d,bbp=%d) -> %s\n",
               dev.idx, dev.fb_node, dev.width, dev.height,
               dev.color_scheme == UVC_COLOR_RGB ? "rgb24" : "yuyv",
               dev.fps, dev.exposure, dev.gain, dev.bits_per_pixel,
               dev.video_node);
    }
    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    int next_option;
    enum ACTION current_action = ACTION_NONE;
    struct uvc_device_spec dev;
    int ret = 0;

    memset(&dev, 0x00, sizeof(struct uvc_device_spec));
    dev.frames_dir[0] = '\0';

    do {
        next_option = getopt_long(argc, argv, short_options, long_options, NULL);
        switch (next_option) {
        case 'h':
            printf("%s", help);
            exit(0);
        case 'c':
            current_action = ACTION_CREATE;
            printf("Creating a new UVC device.\n");
            break;
        case 'm':
            current_action = ACTION_MODIFY;
            dev.idx = atoi(optarg) - 1;
            break;
        case 'R':
            current_action = ACTION_DESTROY;
            dev.idx = atoi(optarg) - 1;
            printf("Removing the UVC device.\n");
            break;
        case 'l':
            list_devices();
            break;
        case 'f':
            dev.fps = atoi(optarg);
            printf("Setting FPS to %d.\n", dev.fps);
            break;
        case 'e':
            dev.exposure = atoi(optarg);
            printf("Setting exposure to %d.\n", dev.exposure);
            break;
        case 'g':
            dev.gain = atoi(optarg);
            printf("Setting gain to %d.\n", dev.gain);
            break;
        case 'r':
            if (!parse_resolution(optarg, &dev)) {
                fprintf(stderr, "Failed to parse resolution.\n");
                exit(-1);
            }
            printf("Setting resolution to %dx%d.\n", dev.width, dev.height);
            break;
        case 'b':
            dev.bits_per_pixel = atoi(optarg);
            printf("Setting bits per pixel to %d.\n", dev.bits_per_pixel);
            break;
        case 0x100: // --color-scheme
            if (strcmp(optarg, "RGB") == 0) {
                printf("Setting color scheme to RGB.\n");
                dev.color_scheme = UVC_COLOR_RGB;
            } else if (strcmp(optarg, "YUV") == 0) {
                printf("Setting color scheme to YUV.\n");
                dev.color_scheme = UVC_COLOR_YUV;
            } else {
                fprintf(stderr, "Unsupported color scheme %s. Use RGB or YUV.\n", optarg);
                exit(-1);
            }
            break;
        case 0x101: // --frames-dir
            if (load_frames_from_dir(optarg, &dev) != 0) {
                fprintf(stderr, "Failed to load frames from %s.\n", optarg);
                exit(-1);
            }
            break;
        case 'd':
            printf("Using device %s.\n", optarg);
            strncpy(ctl_path, optarg, sizeof(ctl_path) - 1);
            break;
        case '?':
            fprintf(stderr, "Unknown option or missing argument.\n");
            exit(-1);
        }
    } while (next_option != -1);

    switch (current_action) {
    case ACTION_CREATE:
        ret = create_device(&dev);
        break;
    case ACTION_DESTROY:
        ret = remove_device(&dev);
        break;
    case ACTION_MODIFY:
        ret = modify_device(&dev);
        break;
    case ACTION_NONE:
        break;
    }

    return ret;
}