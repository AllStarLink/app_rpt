#!/bin/sh

cd /usr/src

# Download app_rpt repo if not present already
if [ ! -d app_rpt ]; then
	git clone https://github.com/InterLinked1/app_rpt.git
else
	# else, update it
	cd app_rpt
	git checkout master
	git pull
	cd ..
fi

# cd into Asterisk source directory
cd $( ls -d -v */ | grep "^asterisk" | tail -1 )

apt-get install -y libusb-dev # chan_simpleusb and chan_usbradio require libusb-dev on Debian
modprobe snd-pcm-oss # /dev/dsp1 needs to exist for chan_simpleusb and chan_usbradio to work

cp ../app_rpt/Makefiles.diff /tmp/rpt.diff
git apply /tmp/rpt.diff

echoerr() {
	printf "\e[31;1m%s\e[0m\n" "$*" >&2;
}

rpt_add() {
	if [ ! -f ../app_rpt/$1 ]; then
		echoerr "WARNING: File $1 does not exist"
	fi
	printf "Adding module %s\n" "$1"
	cp ../app_rpt/$1 $1
}

if [ ! -d apps/app_rpt ]; then
	mkdir apps/app_rpt
fi
if [ ! -d channels/xpmr ]; then
	mkdir channels/xpmr
fi

rpt_add "apps/app_rpt.c"
rpt_add "apps/app_gps.c"
rpt_add "apps/app_rpt/app_rpt.h"
rpt_add "apps/app_rpt/mdc_encode.c"
rpt_add "apps/app_rpt/mdc_encode.h"
rpt_add "apps/app_rpt/mdc_decode.c"
rpt_add "apps/app_rpt/mdc_decode.h"
rpt_add "apps/app_rpt/pocsag.c"
rpt_add "apps/app_rpt/pocsag.h"
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
rpt_add "apps/app_rpt/rpt_capabilities.c"
rpt_add "apps/app_rpt/rpt_capabilities.h"
rpt_add "apps/app_rpt/rpt_vox.c"
rpt_add "apps/app_rpt/rpt_vox.h"
rpt_add "channels/chan_echolink.c"
rpt_add "channels/chan_simpleusb.c"

# Until we figure out the dependencies:
rpt_add "channels/chan_usbradio.c"

rpt_add "channels/chan_tlb.c"
rpt_add "channels/chan_voter.c"
rpt_add "channels/chan_usrp.c"
rpt_add "channels/chan_usrp.h"
rpt_add "channels/xpmr/sinetabx.h"
rpt_add "channels/xpmr/xpmr.c"
rpt_add "channels/xpmr/xpmr.h"
rpt_add "channels/xpmr/xpmr_coef.h"

make apps
make channels
make install
