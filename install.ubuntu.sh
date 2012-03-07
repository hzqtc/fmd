#!/bin/sh
# Install fmd as service on Ubuntu.

if [ `id -u` -ne 0 ]; then
	echo "It must be executed by root."
	exit 1
fi

# Copy files into /var/lib/fmd
DEST_DIR=/var/lib/fmd
mkdir -p $DEST_DIR
SCRIPT_DIR=`dirname $0`
cp $SCRIPT_DIR/* $DEST_DIR

# Setup service
mv $DEST_DIR/fmd.init /etc/init.d/fmd
update-rc.d -f fmd remove
update-rc.d fmd defaults

# Start service
service fmd start

echo "Installation finished. You may change default configuration by modify file /root/.fmd.conf"
