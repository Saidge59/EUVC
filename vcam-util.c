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

#include "vcam.h"

static const char *short_options = "hcm:r:ls:p:d:f:e:g:R:cs:b:fd:";

const struct option long_options[] = {
    {"help", 0, NULL, 'h'},
    {"create", 0, NULL, 'c'},
    {"modify", 1, NULL, 'm'},
    {"list", 0, NULL, 'l'},
    {"size", 1, NULL, 's'},
    {"pixfmt", 1, NULL, 'p'},
    {"device", 1, NULL, 'd'},
    {"remove", 1, NULL, 'r'},
    {"fps", 1, NULL, 'f'},
    {"exposure", 1, NULL, 'e'},
    {"gain", 1, NULL, 'g'},
    {"resolution", 1, NULL, 'R'},
    {"color-scheme", 1, NULL, 0x100},
    {"bbp", 1, NULL, 'b'},
    {"frames-dir", 1, NULL, 0x101},
    {NULL, 0, NULL, 0}
};

const char *help =
    " -h --help                            Print this informations.\n"
    " -c --create                          Create a new emulated UVC device.\n"
    " -m --modify  idx                     Modify an existing device.\n"
    " -r --remove  idx                     Remove a device.\n"
    " -l --list                            List devices.\n"
    " -s --size    WIDTHxHEIGHTxCROPRATIO  Specify virtual resolution or crop ratio.\n"
    " -R --resolution WIDTHxHEIGHT         Specify resolution.\n"
    " -p --pixfmt  pix_fmt                 Specify pixel format (rgb24,yuyv).\n"
    " -f --fps     FPS                     Set frames per second.\n"
    " -e --exposure VALUE                  Set exposure value.\n"
    " -g --gain    VALUE                   Set gain value.\n"
    " --color-scheme SCHEME                Set color scheme (e.g., RGB, YUV).\n"
    " -b --bbp     BITS                    Set bits per pixel.\n"
    " --frames-dir PATH                    Load frames from directory.\n"
    " -d --device  /dev/*                  Control device node (default: /dev/vcamctl).\n";

struct vcam_device_spec device_template = {
    .width = 640,
    .height = 480,
    .pix_fmt = VCAM_PIXFMT_RGB24,
    .video_node = "",
    .fb_node = "",
    .fps = 30,
    .exposure = 100,
    .gain = 50,
    .bits_per_pixel = 24
};

static char ctl_path[128] = "/dev/vcamctl";

static bool parse_cropratio(char *res_str, struct vcam_device_spec *dev)
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

bool parse_resolution(char *res_str, struct vcam_device_spec *dev)
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

int determine_pixfmt(char *pixfmt_str)
{
    if (!strncmp(pixfmt_str, "rgb24", 5))
        return VCAM_PIXFMT_RGB24;
    if (!strncmp(pixfmt_str, "yuyv", 4))
        return VCAM_PIXFMT_YUYV;
    return -1;
}

static int load_frames_from_dir(const char *dir_path, struct vcam_device_spec *dev)
{
    DIR *dir;
    struct dirent *entry;
    int frame_count = 0;
    struct stat st;
    char full_path[512];

    if (!(dir = opendir(dir_path))) {
        fprintf(stderr, "Failed to open directory %s\n", dir_path);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name) >= sizeof(full_path)) {
            fprintf(stderr, "Path too long: %s/%s\n", dir_path, entry->d_name);
            continue;
        }

        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            frame_count++;
            printf("Found frame: %s\n", entry->d_name);
        }
    }
    closedir(dir);

    if (frame_count == 0) {
        fprintf(stderr, "No frames found in %s\n", dir_path);
        return -1;
    }

    strncpy(dev->frames_dir, dir_path, sizeof(dev->frames_dir) - 1);
    dev->frame_count = frame_count;
    return 0;
}

int create_device(struct vcam_device_spec *dev)
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

    if (!dev->pix_fmt)
        dev->pix_fmt = device_template.pix_fmt;

    if (!dev->fps)
        dev->fps = device_template.fps;
    if (!dev->exposure)
        dev->exposure = device_template.exposure;
    if (!dev->gain)
        dev->gain = device_template.gain;
    if (!dev->bits_per_pixel)
        dev->bits_per_pixel = device_template.bits_per_pixel;

    int res = ioctl(fd, VCAM_IOCTL_CREATE_DEVICE, dev);
    if (res) {
        fprintf(stderr, "Failed to create a new device.\n");
    } else if (dev->frames_dir[0]) {
        printf("Loading frames from %s\n", dev->frames_dir);
    }

    close(fd);
    return res;
}

int remove_device(struct vcam_device_spec *dev)
{
    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    int res = ioctl(fd, VCAM_IOCTL_DESTROY_DEVICE, dev);
    if (res) {
        fprintf(stderr, "Failed to remove the device on index %d.\n",
                dev->idx + 1);
    }

    close(fd);
    return res;
}

int modify_device(struct vcam_device_spec *dev)
{
    struct vcam_device_spec orig_dev = {.idx = dev->idx};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    if (ioctl(fd, VCAM_IOCTL_GET_DEVICE, &orig_dev)) {
        fprintf(stderr, "Failed to find device on index %d.\n",
                orig_dev.idx + 1);
        close(fd);
        return -1;
    }

    if (!dev->width || !dev->height) {
        dev->width = orig_dev.width;
        dev->height = orig_dev.height;
    }

    if (!dev->pix_fmt)
        dev->pix_fmt = orig_dev.pix_fmt;

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

    int res = ioctl(fd, VCAM_IOCTL_MODIFY_SETTING, dev);
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
    struct vcam_device_spec dev = {.idx = 0};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device.\n", ctl_path);
        return -1;
    }

    printf("Available virtual UVC compatible devices:\n");
    while (!ioctl(fd, VCAM_IOCTL_GET_DEVICE, &dev)) {
        dev.idx++;
        printf("%d. %s(%d,%d,%d/%d,%s,fps=%d,exp=%d,gain=%d,bbp=%d) -> %s\n",
               dev.idx, dev.fb_node, dev.width, dev.height,
               dev.cropratio.numerator, dev.cropratio.denominator,
               dev.pix_fmt == VCAM_PIXFMT_RGB24 ? "rgb24" : "yuyv",
               dev.fps, dev.exposure, dev.gain, dev.bits_per_pixel,
               dev.video_node);
    }
    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    int next_option;
    struct vcam_device_spec dev;
    int ret = 0;
    int tmp;

    memset(&dev, 0x00, sizeof(struct vcam_device_spec));
    dev.frames_dir[0] = '\0';

    do {
        next_option = getopt_long(argc, argv, short_options, long_options, NULL);
        switch (next_option) {
        case 'h':
            printf("%s", help);
            exit(0);
        case 'c':
            printf("Creating a new UVC device.\n");
            ret = create_device(&dev);
            break;
        case 'm':
            printf("Modifying the UVC device.\n");
            dev.idx = atoi(optarg) - 1;
            ret = modify_device(&dev);
            break;
        case 'r':
            printf("Removing the UVC device.\n");
            dev.idx = atoi(optarg) - 1;
            ret = remove_device(&dev);
            break;
        case 'l':
            list_devices();
            break;
        case 's':
            if (!parse_resolution(optarg, &dev)) {
                fprintf(stderr, "Failed to parse resolution and crop ratio.\n");
                exit(-1);
            }
            printf("Setting resolution to %dx%dx%d/%d.\n", dev.width,
                   dev.height, dev.cropratio.numerator,
                   dev.cropratio.denominator);
            break;
        case 'p':
            tmp = determine_pixfmt(optarg);
            if (tmp < 0) {
                fprintf(stderr, "Failed to recognize pixel format %s.\n", optarg);
                exit(-1);
            }
            dev.pix_fmt = (char)tmp;
            printf("Setting pixel format to %s.\n", optarg);
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
        case 'R':
            if (!parse_resolution(optarg, &dev)) {
                fprintf(stderr, "Failed to parse resolution.\n");
                exit(-1);
            }
            printf("Setting resolution to %dx%d.\n", dev.width, dev.height);
            break;
        case 0x100: // --color-scheme
            if (strcmp(optarg, "RGB") == 0 || strcmp(optarg, "YUV") == 0) {
                dev.color_scheme = malloc(strlen(optarg) + 1);
                if (dev.color_scheme) {
                    strcpy(dev.color_scheme, optarg);
                    printf("Setting color scheme to %s.\n", dev.color_scheme);
                } else {
                    fprintf(stderr, "Memory allocation failed for color scheme.\n");
                    exit(-1);
                }
            } else {
                fprintf(stderr, "Unsupported color scheme %s. Use RGB or YUV.\n", optarg);
                exit(-1);
            }
            break;
        case 'b':
            dev.bits_per_pixel = atoi(optarg);
            printf("Setting bits per pixel to %d.\n", dev.bits_per_pixel);
            break;
        case 0x101: // --frames-dir
            if (load_frames_from_dir(optarg, &dev) == 0) {
                printf("Frames loaded from %s.\n", optarg);
            } else {
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

    if (dev.color_scheme) free(dev.color_scheme);

    return ret;
}