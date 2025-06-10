#ifndef UVC_CONTROL_H
#define UVC_CONTROL_H

#include "uvc.h"

int create_control_device(const char *dev_name);
void destroy_control_device(void);

/* request new virtual camera device */
int request_uvc_device(struct uvc_device_spec *dev_spec);

#endif
