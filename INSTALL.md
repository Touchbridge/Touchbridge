# Touchbridge install instructions for Raspberry Pi

*NOTE: the following steps assume you are installing on a Pi 2 running the Jessy
distro and that a recent version of Node-RED is already installed.*

There are four steps to installing Touchbridge and its Node-RED support
on a Raspberry Pi:

1. Get Touchbridge
2. Install dependencies (the extra libraries we need to build Touchbridge)
3. Build and install Touchbridge
4. Setting up the pi to start the Touchbridge server on boot-up

## 1. Getting Touchbridge

~~~~
git clone https://github.com/Touchbridge/Touchbridge.git
~~~~

## 2. Installing dependencies

To build the Touchbridge host code for the Pi, you need
a number of libraries. Most of these are available via apt.
However, czmq currently isn't and must be built from source.
Log in to your Pi and do the following:

~~~~
sudo apt-get update
sudo apt-get install libzmq3-dev libglib2.0-dev libtool libtool-bin autotools-dev libsodium-dev autoconf
wget https://github.com/zeromq/czmq/archive/v3.0.2.tar.gz 
tar xf v3.0.2.tar.gz
cd czmq-3.0.2/
./autogen.sh
./configure
make check
sudo make install
sudo ldconfig
cd ..
~~~~

## 3. Building and installing Touchbridge

You can now build Touchbridge:

~~~~
cd Touchbridge
make install
~~~~


## 4. Setting up the pi to start the Touchbridge server on boot-up

Use your text editor of choice to add the following line to
`/etc/rc.local` above the line that says `exit 0`

~~~~
/usr/local/bin/tbg_server &
~~~~

You can now reboot the pi and use Touchbridge via Node-RED by
connecting to port 1880 in the usual way. You should find there
are now several Touchbridge nodes available in Node-RED near
the bottom of the node pallet.

