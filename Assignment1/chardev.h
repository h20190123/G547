#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>


#define MAGIC_NUM 100


#define IOCTL_GET_allignment _IOW(MAGIC_NUM, 0, char *)

#define IOCTL_GET_Channel _IOW(MAGIC_NUM, 1, int *)

#define DEVICE_FILE_NAME "/dev/adc8"

#endif
