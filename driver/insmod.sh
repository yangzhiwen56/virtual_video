#!/bin/sh

modprobe videodev
modprobe v4l2-common
modprobe videobuf-core #debug=3
modprobe videobuf-vmalloc #debug=3
insmod virtual_video.ko #debug=1



