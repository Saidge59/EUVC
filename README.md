sudo modprobe -a videobuf2_vmalloc videobuf2_v4l2
sudo insmod vcam.ko

sudo ./vcam-util -l -d /dev/vcamctl
sudo ./vcam-util -c -d /dev/vcamctl
sudo ./vcam-util -m 2 -s 800x600 -p yuyv -d /dev/vcamctl