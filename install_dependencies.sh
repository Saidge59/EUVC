#!/bin/bash

# Update package lists
sudo apt update

# Install required packages
sudo apt install -y linux-headers-$(uname -r) v4l-utils make libc6-dev vlc

# Check and install gcc-12
if ! command -v gcc-12 &> /dev/null; then
    echo "gcc-12 not found. Adding Ubuntu Toolchain PPA..."
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
    sudo apt update
    sudo apt install -y gcc-12 g++-12
fi

# Set gcc-12 as default if not already
if [ "$(gcc --version | head -n1 | cut -d' ' -f4 | cut -d'.' -f1)" -lt 12 ]; then
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 120
    echo "gcc-12 set as default compiler."
fi

echo "Dependencies installed successfully. Run 'make' to build euvc."
