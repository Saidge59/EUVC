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
#include <errno.h>

#include "euvc.h"

#define PATH_MAX 256
#define INIT_VIDEOBUF2_MODULES "sudo modprobe -a videobuf2_vmalloc videobuf2_v4l2"
#define INIT_EUVC_MODULE "sudo insmod euvc.ko"
#define DEINIT_EUVC_MODULE "sudo rmmod euvc.ko"

static const char *short_options = "hiDcm:r:ld:f:e:g:R:cs:b:fd:L:C:";

const struct option long_options[] = {
    {"help", 0, NULL, 'h'},
    {"init", 0, NULL, 'i'},
    {"deinit", 0, NULL, 'D'},
    {"create", 0, NULL, 'c'},
    {"modify", 1, NULL, 'm'},
    {"list", 0, NULL, 'l'},
    {"device", 1, NULL, 'd'},
    {"remove", 1, NULL, 'R'},
    {"fps", 1, NULL, 'f'},
    {"exposure", 1, NULL, 'e'},
    {"gain", 1, NULL, 'g'},
    {"resolution", 1, NULL, 'r'},
    {"crop-ratio", 1, NULL, 'C'},
    {"color-scheme", 1, NULL, 0x100},
    {"bpp", 1, NULL, 'b'},
    {"frames-dir", 1, NULL, 0x101},
    {"loop", 1, NULL, 'L'},
    {NULL, 0, NULL, 0}
};

const char *help =
    "\n"
    "**********************************************************************\n"
    "**                     euvc Device Management Help                   **\n"
    "**********************************************************************\n"
    "\n"
    "  -h, --help                        Display this help message         \n"
    "  -i, --init                        Initialize modules (load videobuf2_vmalloc, videobuf2_v4l2, and euvc.ko)\n"
    "  -D, --deinit                      Deinitialize modules (unload euvc.ko and videobuf2 modules)\n"
    "  -c, --create                      Create new emulated euvc device    \n"
    "  -m, --modify <idx>                Modify existing device            \n"
    "  -R, --remove <idx>                Remove a device (emulate unplug)  \n"
    "  -l, --list                        List all devices                  \n"
    "  -r, --resolution <width>x<height> Set resolution (e.g. 800x700)     \n"
    "  -C, --crop-ratio <num/den>        Set crop ratio (e.g. 1/1)         \n"
    "  -f, --fps <fps>                   Set frames per second             \n"
    "  -e, --exposure <val>              Set exposure (e.g. 100)           \n"
    "  -g, --gain <val>                  Set gain (e.g. 50)                \n"
    "  --color-scheme <scheme>           Set color scheme (RGB, GRAY8)     \n"
    "  -b, --bpp <bits>                  Set bits per pixel (8bpp, 24bpp)  \n"
    "  --frames-dir <path>               Load frames from directory        \n"
    "  -d, --device /dev/*               Device node (default: /dev/euvcctl)\n"
    "  -L, --loop <0|1>                  Enable (1) or disable (0) looping \n\n";

enum ACTION { ACTION_NONE, ACTION_CREATE, ACTION_DESTROY, ACTION_MODIFY };

struct euvc_device_spec device_template = {
    .width = 640,
    .height = 480,
    .color_scheme = EUVC_COLOR_GREY,
    .video_node = "",
    .fps = 30,
    .exposure = 100,
    .gain = 50,
    .bits_per_pixel = 8,
    .loop = false,
    .frame_idx = 0,
    .frame_count = 0,
    .cropratio = {.numerator = 1, .denominator = 1}
};

static char ctl_path[128] = "/dev/euvcctl";

static bool parse_cropratio(char *res_str, struct euvc_device_spec *dev)
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

bool parse_resolution(char *res_str, struct euvc_device_spec *dev)
{
    char *tmp = strtok(res_str, "x:,");
    if (!tmp)
        return false;
    dev->width = atoi(tmp);
    tmp = strtok(NULL, "x:,");
    if (!tmp)
        return false;
    dev->height = atoi(tmp);
    return true;
}

int load_frames_from_dir(const char *dir_path_raw, struct euvc_device_spec *dev_spec)
{
    struct stat st;
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    char dir_path[PATH_MAX];

    if (stat(dir_path_raw, &st) != 0) {
        fprintf(stderr, "Directory '%s' does not exist: %s\n", dir_path_raw, strerror(errno));
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "'%s' is not a directory.\n", dir_path_raw);
        return -1;
    }

    snprintf(dir_path, sizeof(dir_path), "%s%s",
             dir_path_raw,
             (dir_path_raw[strlen(dir_path_raw) - 1] == '/') ? "" : "/");

    if (!(dir = opendir(dir_path))) {
        fprintf(stderr, "Failed to open directory '%s': %s\n", dir_path, strerror(errno));
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "output_", 7) == 0 &&
            strstr(entry->d_name, ".raw") != NULL) {
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

int create_device(struct euvc_device_spec *dev)
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

    if (dev->color_scheme == EUVC_COLOR_EMPTY)
        dev->color_scheme = device_template.color_scheme;

    if (dev->fps < 0)
        dev->fps = device_template.fps;
    if (dev->exposure < 0)
        dev->exposure = device_template.exposure;
    if (dev->gain < 0)
        dev->gain = device_template.gain;
    if (dev->bits_per_pixel < 0)
        dev->bits_per_pixel = device_template.bits_per_pixel;
    if (dev->frame_idx < 0)
        dev->frame_idx = device_template.frame_idx;

    printf("Creating device: width=%d, height=%d, bpp=%d, color=%d\n",
           dev->width, dev->height, dev->bits_per_pixel, dev->color_scheme);

    int res = ioctl(fd, EUVC_IOCTL_CREATE_DEVICE, dev);
    if (res) {
        fprintf(stderr, "Failed to create a new device. Error code: %d\n", res);
    } else if (dev->frames_dir[0]) {
        printf("Loading frames from %s\n", dev->frames_dir);
    }

    close(fd);
    return res;
}

int remove_device(struct euvc_device_spec *dev)
{
    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    int res = ioctl(fd, EUVC_IOCTL_DESTROY_DEVICE, dev);
    if (res) {
        fprintf(stderr, "Failed to remove the device on index %d.\n",
                dev->idx + 1);
    }

    close(fd);
    return res;
}

int modify_device(struct euvc_device_spec *dev)
{
    struct euvc_device_spec orig_dev = {.idx = dev->idx};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    if (ioctl(fd, EUVC_IOCTL_GET_DEVICE, &orig_dev)) {
        fprintf(stderr, "Failed to find device on index %d.\n",
                orig_dev.idx + 1);
        close(fd);
        return -1;
    }

    if (orig_dev.orig_width || orig_dev.orig_height) {
        dev->width = orig_dev.orig_width;
        dev->height = orig_dev.orig_height;
    }
    
    if (dev->color_scheme == -1) {
        dev->color_scheme = orig_dev.color_scheme;
    }

    if (dev->fps == -1) {
        dev->fps = orig_dev.fps;
    }
    if (dev->exposure == -1) {
        dev->exposure = orig_dev.exposure;
    }
    if (dev->gain == -1) {
        dev->gain = orig_dev.gain;
    }
    if (dev->bits_per_pixel == -1) {
        dev->bits_per_pixel = orig_dev.bits_per_pixel;
    }
    if (!dev->cropratio.numerator || !dev->cropratio.denominator) {
        dev->cropratio = orig_dev.cropratio;
    }
    if (dev->frame_idx == -1) {
        dev->frame_idx = orig_dev.frame_idx;
    }

    int res = ioctl(fd, EUVC_IOCTL_MODIFY_SETTING, dev);
    if (res) {
        fprintf(stderr, "Failed to modify the device.\n");
    }

    close(fd);
    return res;
}

int list_devices()
{
    struct euvc_device_spec dev = {.idx = 0};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    printf("Available virtual euvc compatible devices:\n");
    while (!ioctl(fd, EUVC_IOCTL_GET_DEVICE, &dev)) {
        dev.idx++;
        printf("%d. (%d,%d,%s,fps=%d,exp=%d,gain=%d,bpp=%d) -> %s\n",
               dev.idx, dev.width, dev.height,
               dev.color_scheme == EUVC_COLOR_RGB ? "rgb24" : "gray8",
               dev.fps, dev.exposure, dev.gain, dev.bits_per_pixel,
               dev.video_node);
    }
    close(fd);
    return 0;
}

void init_modules()
{
    int result;
    printf("Initializing modules...\n");
    result = system(INIT_VIDEOBUF2_MODULES);
    if (result != 0) {
        fprintf(stderr, "Failed to load videobuf2 modules. Error code: %d\n", result);
        return;
    }
    result = system(INIT_EUVC_MODULE);
    if (result != 0) {
        fprintf(stderr, "Failed to load euvc.ko. Error code: %d\n", result);
        return;
    }
    printf("Modules loaded successfully.\n");
}

void deinit_modules() {
    int result;
    printf("Deinitializing module...\n");
    result = system(DEINIT_EUVC_MODULE);
    if (result != 0 && result != 256) {
        fprintf(stderr, "Failed to unload euvc.ko. Error code: %d\n", result);
    }
    printf("Module deinitialized successfully.\n");
}

int main(int argc, char *argv[])
{
    int next_option;
    enum ACTION current_action = ACTION_NONE;
    int ret = 0;

    struct euvc_device_spec dev = {
        .idx = -1,
        .width = 0,
        .height = 0,
        .fps = -1,
        .exposure = -1,
        .gain = -1,
        .bits_per_pixel = -1,
        .color_scheme = EUVC_COLOR_EMPTY,
        .cropratio = {0, 0},
        .loop = -1,
        .frame_idx = -1,
    };
    strncpy(dev.frames_dir, "", sizeof(dev.frames_dir) - 1);

    do {
        next_option = getopt_long(argc, argv, short_options, long_options, NULL);
        switch (next_option) {
        case 'h':
            printf("%s", help);
            exit(0);
        case 'i':
            init_modules();
            break;
        case 'D':
            deinit_modules();
            break;
        case 'c':
            current_action = ACTION_CREATE;
            printf("Creating a new euvc device.\n");
            break;
        case 'm':
            current_action = ACTION_MODIFY;
            dev.idx = atoi(optarg) - 1;
            break;
        case 'R':
            current_action = ACTION_DESTROY;
            dev.idx = atoi(optarg) - 1;
            printf("Removing the euvc device.\n");
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
                dev.color_scheme = EUVC_COLOR_RGB;
            } else if (strcmp(optarg, "GRAY8") == 0) {
                printf("Setting color scheme to GRAY8.\n");
                dev.color_scheme = EUVC_COLOR_GREY;
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
        case 'L':
            if (strcmp(optarg, "1") == 0) {
                dev.loop = true;
                printf("Enabling frame looping.\n");
            } else if (strcmp(optarg, "0") == 0) {
                dev.loop = false;
                printf("Disabling frame looping.\n");
            } else {
                fprintf(stderr, "Invalid loop value. Use 0 or 1.\n");
                exit(-1);
            }
            break;
        case 'C': // --crop-ratio
            if (!parse_cropratio(optarg, &dev)) {
                fprintf(stderr, "Invalid crop ratio format. Use numerator/denominator.\n");
                exit(-1);
            }
            printf("Setting crop ratio to %d/%d.\n", dev.cropratio.numerator, dev.cropratio.denominator);
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