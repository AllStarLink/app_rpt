# app_rpt
Refactoring and upgrade of AllStarLink's app_rpt, etc.

# Development

## Prettifying

`indent --k-and-r-style --use-tabs --tab-size4 --braces-on-if-line --cuddle-else --dont-break-function-decl-args --line-length120 --swallow-optional-blank-lines apps/app_rpt.c`

# Installing

You can use PhreakNet to install Asterisk automatically, first. You'll need to manually add the app_rpt components afterwards.

```
cd /usr/src && wget https://docs.phreaknet.org/script/phreaknet.sh && chmod +x phreaknet.sh && ./phreaknet.sh make
phreaknet install -s -t # install chan_sip (if you need it still) and DAHDI (required)
```

## Pre-Reqs

`chan_simpleusb` requires `libusb-dev` on Debian:

`apt-get install -y libusb-dev`

## Compiling

Add this near the bottom of `apps/Makefile`:

`$(call MOD_ADD_C,app_rpt,$(wildcard app_rpt/rpt_*.c))`

Add this near the bottom of `channels/Makefile`:

`chan_simpleusb.so: LIBS+=-lusb -lasound`

## After DAHDI/Asterisk installed

`/dev/dsp1` needs to exist for chan_simpleusb and chan_usbradio to work.

This StackOverflow post contains the answer in an upvoted comment: https://unix.stackexchange.com/questions/103746/why-wont-linux-let-me-play-with-dev-dsp/103755#103755

Run: `modprobe snd-pcm-oss` (as root/sudo)

# Files from AllStarLink Asterisk which are in scope

```
allstar/mdc_decode.c
allstar/mdc_encode.c
allstar/pocsag.c
apps/app_gps.c
apps/app_rpt.c
channels/chan_tlb.c
channels/chan_usbradio.c
channels/chan_usrp.c
channels/chan_usrp.h
channels/chan_voter.c
channels/chan_simpleusb.c
channels/chan_echolink.c
include/allstar/mdc_decode.h
include/allstar/mdc_encode.h
include/allstar/pocsag.h
```

# Files that may have changed over time

```
Files ./apps/app_rpt.c and ../ASL-Asterisk/asterisk/apps/app_rpt.c differ
Files ./build_tools/make_version and ../ASL-Asterisk/asterisk/build_tools/make_version differ
Files ./channels/chan_dahdi.c and ../ASL-Asterisk/asterisk/channels/chan_dahdi.c differ
Files ./channels/chan_iax2.c and ../ASL-Asterisk/asterisk/channels/chan_iax2.c differ
Files ./channels/iax2.h and ../ASL-Asterisk/asterisk/channels/iax2.h differ
Files ./channels/iax2-parser.c and ../ASL-Asterisk/asterisk/channels/iax2-parser.c differ
Files ./channels/iax2-parser.h and ../ASL-Asterisk/asterisk/channels/iax2-parser.h differ
Files ./channels/iax2-provision.c and ../ASL-Asterisk/asterisk/channels/iax2-provision.c differ
Files ./channels/Makefile and ../ASL-Asterisk/asterisk/channels/Makefile differ
Files ./codecs/codec_adpcm.c and ../ASL-Asterisk/asterisk/codecs/codec_adpcm.c differ
Files ./codecs/codec_ilbc.c and ../ASL-Asterisk/asterisk/codecs/codec_ilbc.c differ
Files ./codecs/gsm/Makefile and ../ASL-Asterisk/asterisk/codecs/gsm/Makefile differ
Files ./codecs/Makefile and ../ASL-Asterisk/asterisk/codecs/Makefile differ
Files ./configs/dnsmgr.conf.sample and ../ASL-Asterisk/asterisk/configs/dnsmgr.conf.sample differ
Files ./configs/extensions.conf.sample and ../ASL-Asterisk/asterisk/configs/extensions.conf.sample differ
Files ./configs/iax.conf.sample and ../ASL-Asterisk/asterisk/configs/iax.conf.sample differ
Files ./configs/indications.conf.sample and ../ASL-Asterisk/asterisk/configs/indications.conf.sample differ
Files ./configs/manager.conf.sample and ../ASL-Asterisk/asterisk/configs/manager.conf.sample differ
Files ./configs/modules.conf.sample and ../ASL-Asterisk/asterisk/configs/modules.conf.sample differ
Files ./configs/rpt.conf.sample and ../ASL-Asterisk/asterisk/configs/rpt.conf.sample differ
Files ./configs/sip.conf.sample and ../ASL-Asterisk/asterisk/configs/sip.conf.sample differ
Files ./configure and ../ASL-Asterisk/asterisk/configure differ
Files ./configure.ac and ../ASL-Asterisk/asterisk/configure.ac differ
Files ./contrib/init.d/rc.debian.asterisk and ../ASL-Asterisk/asterisk/contrib/init.d/rc.debian.asterisk differ
Files ./contrib/init.d/rc.gentoo.asterisk and ../ASL-Asterisk/asterisk/contrib/init.d/rc.gentoo.asterisk differ
Files ./contrib/scripts/get_ilbc_source.sh and ../ASL-Asterisk/asterisk/contrib/scripts/get_ilbc_source.sh differ
Files ./formats/format_ilbc.c and ../ASL-Asterisk/asterisk/formats/format_ilbc.c differ
Files ./funcs/func_curl.c and ../ASL-Asterisk/asterisk/funcs/func_curl.c differ
Files ./include/asterisk/astobj2.h and ../ASL-Asterisk/asterisk/include/asterisk/astobj2.h differ
Files ./include/asterisk/frame.h and ../ASL-Asterisk/asterisk/include/asterisk/frame.h differ
Files ./include/asterisk/inline_api.h and ../ASL-Asterisk/asterisk/include/asterisk/inline_api.h differ
Files ./include/jitterbuf.h and ../ASL-Asterisk/asterisk/include/jitterbuf.h differ
Files ./main/abstract_jb.c and ../ASL-Asterisk/asterisk/main/abstract_jb.c differ
Files ./main/asterisk.c and ../ASL-Asterisk/asterisk/main/asterisk.c differ
Files ./main/astobj2.c and ../ASL-Asterisk/asterisk/main/astobj2.c differ
Files ./main/channel.c and ../ASL-Asterisk/asterisk/main/channel.c differ
Files ./main/config.c and ../ASL-Asterisk/asterisk/main/config.c differ
Files ./main/dnsmgr.c and ../ASL-Asterisk/asterisk/main/dnsmgr.c differ
Files ./main/dsp.c and ../ASL-Asterisk/asterisk/main/dsp.c differ
Files ./main/file.c and ../ASL-Asterisk/asterisk/main/file.c differ
Files ./main/frame.c and ../ASL-Asterisk/asterisk/main/frame.c differ
Files ./main/indications.c and ../ASL-Asterisk/asterisk/main/indications.c differ
Files ./main/jitterbuf.c and ../ASL-Asterisk/asterisk/main/jitterbuf.c differ
Files ./main/pbx.c and ../ASL-Asterisk/asterisk/main/pbx.c differ
Files ./main/utils.c and ../ASL-Asterisk/asterisk/main/utils.c differ
Files ./Makefile and ../ASL-Asterisk/asterisk/Makefile differ
Files ./res/res_features.c and ../ASL-Asterisk/asterisk/res/res_features.c differ
Files ./res/snmp/agent.c and ../ASL-Asterisk/asterisk/res/snmp/agent.c differ
Files ./sounds/Makefile and ../ASL-Asterisk/asterisk/sounds/Makefile differ
Files ./sounds/sounds.xml and ../ASL-Asterisk/asterisk/sounds/sounds.xml differ
Files ./utils/Makefile and ../ASL-Asterisk/asterisk/utils/Makefile differ
```
