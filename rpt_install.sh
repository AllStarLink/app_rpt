#!/bin/sh

cd /usr/src

# Directory of the repo.
# If we need to test old versions, easiest to clone this dir, then change this to the new name for testing.
MYDIR=app_rpt

FILE_DIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
FILE_NAME=$( basename $0 ) # grr... why is realpath not in the POSIX standard?
FILE_PATH="$FILE_DIR/$FILE_NAME"

# Download app_rpt repo if not present already
if [ ! -d $MYDIR ]; then
	git clone https://github.com/AllStarLink/app_rpt.git
else
	# else, update it
	cd $MYDIR
	# Stick to whatever branch we're on
	#git checkout master
	git pull
	cd ..
fi

# It's possible this script itself (if run outside of the repo) is obsolete. Make sure we only run the latest one.
if [ "$1" != "updated" ]; then
	printf "Updating ourself\n"
	cp /usr/src/app_rpt/$FILE_NAME $FILE_PATH && exec $FILE_PATH "updated" # Replace and exec, all in one shot (important!)
fi

printf "Script is now the latest version\n"

# cd into Asterisk source directory
ls -d -v */ | grep "^asterisk" | tail -1
cd $( ls -d -v */ | grep "^asterisk" | tail -1 )

apt-get install -y libusb-dev # chan_simpleusb and chan_usbradio require libusb-dev on Debian
modprobe snd-pcm-oss # /dev/dsp1 needs to exist for chan_simpleusb and chan_usbradio to work
echo "snd-pcm-oss" >> /etc/modules # load module at startup for USB

cp ../$MYDIR/apps/Makefile.diff /tmp/app_Makefile.diff
git apply /tmp/app_Makefile.diff

cp ../$MYDIR/channels/Makefile.diff /tmp/channels_Makefile.diff
git apply /tmp/channels_Makefile.diff

cp ../$MYDIR/utils/Makefile.diff /tmp/utils_makefile.diff
git apply /tmp/utils_makefile.diff

git apply ../$MYDIR/res/Makefile.diff

echoerr() {
	printf "\e[31;1m%s\e[0m\n" "$*" >&2;
}

if [ ! -d apps/app_rpt ]; then
	mkdir apps/app_rpt
fi
if [ ! -d channels/xpmr ]; then
	mkdir channels/xpmr
fi

# Remove anything that may have been there before (mainly relevant for testing older HEADs)
rm -f apps/app_rpt/*

# Copy in the radio modules and files
cp ../$MYDIR/apps/*.c apps
cp ../$MYDIR/apps/app_rpt/* apps/app_rpt
cp ../$MYDIR/channels/*.c channels
cp ../$MYDIR/channels/xpmr/* channels/xpmr
cp ../$MYDIR/configs/samples/*.conf.sample configs/samples
cp ../$MYDIR/include/asterisk/*.h include/asterisk
cp ../$MYDIR/res/* res
cp ../$MYDIR/utils/*.c utils

make -j$(nproc) apps && make -j$(nproc) channels && make -j$(nproc) res && make install
if [ $? -ne 0 ]; then
	exit 1
fi

# If the rpt sounds don't exist yet, add them
if [ ! -d /var/lib/asterisk/sounds/en/rpt ]; then
	printf "RPT sounds don't exist yet, adding them now...\n"
	mkdir /var/lib/asterisk/sounds/en/rpt
	cd /var/lib/asterisk/sounds/en/rpt
	wget "https://downloads.allstarlink.org/archive/old-downloads/asterisk-asl-sounds-en-ulaw.tar.gz"
	# Sounds are extracted directly into the dir
	tar -xvzf asterisk-asl-sounds-en-ulaw.tar.gz
	rm asterisk-asl-sounds-en-ulaw.tar.gz
fi

# Add tests to the test suite, if it exists
if [ -d /usr/src/testsuite ]; then
	printf "Setting up tests for app_rpt...\n"
	cd /usr/src/testsuite
	git apply ../$MYDIR/tests/apps/tests_apps.diff
	cp -r ../$MYDIR/tests/apps/rpt tests/apps
fi
