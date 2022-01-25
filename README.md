# NDI to JACK

NDI to JACK is an application that connects an NDI audio source to JACK for use as an audio output into other JACK compatible applications.
The JACK to NDI application, which is included, creates an NDI source from a JACK input.

## Features
- Manage NDI connections using the integrated web server
- Support for up to 30 simultaneous 2 channel unique NDI audio sources
- Uses the latest version of NDI - NDI 5
- Nearly zero latency

## Supported devices

This software is tested with a Raspberry Pi 4 (32 and 64-bit). This software does not require much CPU power and so other lower power devices could possibly work.

### Download required installation files

Make sure git is installed.

```
sudo apt update
sudo apt install git
```
Clone this repository and `cd` into it.

```
git clone https://github.com/windows10luke/NDI-to-JACK.git && cd NDI-to-JACK
```

### Install on Raspberry Pi 4 64-bit

Run this compile and install script

```
sudo bash ./easy-install-rpi4-aarch64.sh
```
Installation is now complete!


### Install on Raspberry Pi 4 32-bit

Run this compile and install script

```
sudo bash ./easy-install-rpi4-armhf.sh
```
Installation is now complete!


### Install on Raspberry Pi 3 32-bit

Run this compile and install script

```
sudo bash ./easy-install-rpi3-armhf.sh
```
Installation is now complete!



### Install on x86_64 bit (Intel/AMD)

Run this compile and install script

```
sudo bash ./easy-install-x86_64.sh
```
Installation is now complete!


### Install on generic ARM64

Compiling on generic ARM64 requires use of the NDI Advanced SDK. Due to licensing restrictions, the NDI Advanced SDK must be downloaded manually from NDI's website: ndi.tv
Extract the downloaded NDI Advanced SDK .tar file and copy it to the NDI-to-JACK directory on the target device. This can be achieved by using FTP, SCP, or Samba.

Compile and install

```
sudo bash ./easy-install-generic-aarch64.sh
```
Installation is now complete!


## Usage for NDI to JACK converter

Once the installation process is complete, it will create an executable file located at /opt/ndi2jack/bin/ndi2jack

The installer also creates a symlink to /usr/bin so that it can be run from a normal terminal.

To run and start the web server:

```
sudo ndi2jack
```

## Usage for JACK to NDI converter

Once the installation process is complete, it will create an executable file located at /opt/ndi2jack/bin/jack2ndi

The installer also creates a symlink to /usr/bin so that it can be run from a normal terminal.

To run (multiple instances can be run with different options for multiple NDI send instances):

```
sudo jack2ndi
```

## Install service file for starting ndi2jack on boot

By default this service file runs ndi2jack as the root user with realtime CPU scheduling. This also assumes that JACK is running as a service as the root user.

```
sudo cp ./ndi2jack.service /etc/systemd/system/
sudo systemctl enable ndi2jack.service
sudo systemctl start ndi2jack.service
```

## Install service file for starting jack2ndi on boot

By default this service file runs jack2ndi as the root user with realtime CPU scheduling. This also assumes that JACK is running as a service as the root user.

```
sudo cp ./jack2ndi.service /etc/systemd/system/
sudo systemctl enable jack2ndi.service
sudo systemctl start jack2ndi.service
```

## Helpful links

https://wiki.linuxaudio.org/wiki/list_of_jack_frame_period_settings_ideal_for_usb_interface
