#!/bin/sh
# Install fmd as service on linux.

if [ `id -u` -ne 0 ]; then
    echo "It must be executed by root."
    exit 1
fi

# Copy files into /usr/local/bin/
DEST_DIR=/usr/local/bin/
mkdir -p $DEST_DIR
install fmd $DEST_DIR
install fmd-update-conf.sh $DEST_DIR

# Setup service
install fmd.init /etc/init.d/fmd
update-rc.d -f fmd remove
update-rc.d fmd defaults

install fmd.upstart /etc/init/fmd.conf

# Start service
service fmd start

echo "Installation finished."
