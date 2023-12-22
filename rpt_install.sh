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
	git clone https://github.com/InterLinked1/app_rpt.git
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
sleep 1

# cd into Asterisk source directory
ls -d -v */ | grep "^asterisk" | tail -1
cd $( ls -d -v */ | grep "^asterisk" | tail -1 )

apt-get install -y libusb-dev # chan_simpleusb and chan_usbradio require libusb-dev on Debian
modprobe snd-pcm-oss # /dev/dsp1 needs to exist for chan_simpleusb and chan_usbradio to work
echo "snd-pcm-oss" >> /etc/modules # load module at startup for USB

cp ../$MYDIR/Makefiles.diff /tmp/rpt.diff
git apply /tmp/rpt.diff

cp ../$MYDIR/utils/Makefile.diff /tmp/utils_makefile.diff
git apply /tmp/utils_makefile.diff

git apply ../$MYDIR/res/Makefile.diff

echoerr() {
	printf "\e[31;1m%s\e[0m\n" "$*" >&2;
}

rpt_add() {
	if [ ! -f ../$MYDIR/$1 ]; then
		echoerr "WARNING: File $1 does not exist"
	fi
	printf "Adding module %s\n" "$1"
	cp ../$MYDIR/$1 $1
}

if [ ! -d apps/app_rpt ]; then
	mkdir apps/app_rpt
fi
if [ ! -d channels/xpmr ]; then
	mkdir channels/xpmr
fi

# Remove anything that may have been there before (mainly relevant for testing older HEADs)
rm -f apps/app_rpt/*

rpt_add "apps/app_rpt.c"
rpt_add "apps/app_gps.c"
rpt_add "apps/app_rpt/app_rpt.h"
rpt_add "apps/app_rpt/mdc_encode.c"
rpt_add "apps/app_rpt/mdc_encode.h"
rpt_add "apps/app_rpt/mdc_decode.c"
rpt_add "apps/app_rpt/mdc_decode.h"
rpt_add "apps/app_rpt/rpt_mdc1200.c"
rpt_add "apps/app_rpt/rpt_mdc1200.h"
rpt_add "apps/app_rpt/pocsag.c"
rpt_add "apps/app_rpt/pocsag.h"
rpt_add "apps/app_rpt/rpt_cli.c"
rpt_add "apps/app_rpt/rpt_cli.h"
rpt_add "apps/app_rpt/rpt_daq.c"
rpt_add "apps/app_rpt/rpt_daq.h"
rpt_add "apps/app_rpt/rpt_lock.c"
rpt_add "apps/app_rpt/rpt_lock.h"
rpt_add "apps/app_rpt/rpt_utils.c"
rpt_add "apps/app_rpt/rpt_utils.h"
rpt_add "apps/app_rpt/rpt_call.c"
rpt_add "apps/app_rpt/rpt_call.h"
rpt_add "apps/app_rpt/rpt_serial.c"
rpt_add "apps/app_rpt/rpt_serial.h"
rpt_add "apps/app_rpt/rpt_xcat.c"
rpt_add "apps/app_rpt/rpt_xcat.h"
rpt_add "apps/app_rpt/rpt_capabilities.c"
rpt_add "apps/app_rpt/rpt_capabilities.h"
rpt_add "apps/app_rpt/rpt_vox.c"
rpt_add "apps/app_rpt/rpt_vox.h"
rpt_add "apps/app_rpt/rpt_uchameleon.c"
rpt_add "apps/app_rpt/rpt_uchameleon.h"
rpt_add "apps/app_rpt/rpt_bridging.c"
rpt_add "apps/app_rpt/rpt_bridging.h"
rpt_add "apps/app_rpt/rpt_channel.c"
rpt_add "apps/app_rpt/rpt_channel.h"
rpt_add "apps/app_rpt/rpt_config.c"
rpt_add "apps/app_rpt/rpt_config.h"
rpt_add "apps/app_rpt/rpt_link.c"
rpt_add "apps/app_rpt/rpt_link.h"
rpt_add "apps/app_rpt/rpt_functions.c"
rpt_add "apps/app_rpt/rpt_functions.h"
rpt_add "apps/app_rpt/rpt_telemetry.c"
rpt_add "apps/app_rpt/rpt_telemetry.h"
rpt_add "apps/app_rpt/rpt_manager.c"
rpt_add "apps/app_rpt/rpt_manager.h"
rpt_add "apps/app_rpt/rpt_translate.c"
rpt_add "apps/app_rpt/rpt_translate.h"
rpt_add "apps/app_rpt/rpt_rig.c"
rpt_add "apps/app_rpt/rpt_rig.h"
rpt_add "channels/chan_echolink.c"
rpt_add "channels/chan_simpleusb.c"
rpt_add "channels/chan_usbradio.c"
rpt_add "channels/chan_tlb.c"
rpt_add "channels/chan_voter.c"
rpt_add "channels/chan_usrp.c"
rpt_add "channels/chan_usrp.h"
rpt_add "channels/xpmr/sinetabx.h"
rpt_add "channels/xpmr/xpmr.c"
rpt_add "channels/xpmr/xpmr.h"
rpt_add "channels/xpmr/xpmr_coef.h"
rpt_add "configs/samples/rpt_http_registrations.conf.sample"
rpt_add "configs/samples/usbradio.conf.sample"
rpt_add "configs/samples/simpleusb.conf.sample"
rpt_add "include/asterisk/res_usbradio.h"
rpt_add "res/res_rpt_http_registrations.c"
rpt_add "res/res_usbradio.c"
rpt_add "res/res_usbradio.exports.in"

rpt_add "utils/pi-tune-menu.c"
rpt_add "utils/radio-tune-menu.c"
rpt_add "utils/simpleusb-tune-menu.c"

nproc
make -j$(nproc) apps
make -j$(nproc) channels
make install

# If the rpt sounds don't exist yet, add them
if [ ! -d /var/lib/asterisk/sounds/en/rpt ]; then
	printf "RPT sounds don't exist yet, adding them now...\n"
	mkdir /var/lib/asterisk/sounds/en/rpt
	# We need subversion, if we don't already have it.
	if ! which subversion > /dev/null; then
		apt-get install -y subversion
	fi
	# use svn to only check out a single folder, see https://stackoverflow.com/questions/7106012/download-a-single-folder-or-directory-from-a-github-repo/18194523#18194523
	svn checkout https://github.com/AllStarLink/ASL-Asterisk/trunk/allstar/sounds/rpt
	mv rpt /var/lib/asterisk/sounds/en/
fi
