# app_rpt
**Refactoring and upgrade of AllStarLink's app_rpt, etc.**

# Debugging and Submitting Bugs

Feel free to open an issue for *any* trouble you might be experiencing with these modules. Please try to adhere to the following when submitting bugs:

- Enable debug to reproduce the issue. You can do this by running `core set debug 5 app_rpt` (less than or greater than 5 depending on the issue and how chatty the debug log level is). You can also enable debug all the time in `asterisk.conf`. To get debug output on the CLI, you will need to add the `debug` level to the `console => ` log file in `logger.conf`. A debug log from the CLI in the seconds leading immediately up to the issue should be provided.

- For segfault issues, a backtrace is needed. Use `ast_coredumper` to get a backtrace and post the relevant threads from `full.txt` (almost always Thread 1): https://wiki.asterisk.org/wiki/display/AST/Getting+a+Backtrace (you can also run `phreaknet backtrace` - make sure to adjust the paste duration from 24 hours if you link the paste link)

- Describe what led up to the issue and how it can be reproduced on our end.

- Any other context that might be helpful in fixing the issue.

Thank you!

# Development

## Prettifying

Note: This is an optional step. If you are touching existing code in a few places, then it is best to skip prettifying and just let the commit checker check for conformance to the coding standards. 

If 'clang_format' isn't already installed, install `clang_format` on your development system.  

From the top level project directory run:

`clang-format -style file:.dev/.clang-format your_file > your_prettified_file`


# Installing

You can use PhreakScript to install Asterisk automatically, first, then use the `rpt_install.sh` script to properly install the files from this repo.

## Automatic Installation

Updated instructions are in the ASL3-Manual repo at https://github.com/AllStarLink/ASL3-Manual/blob/main/docs/user-guide/install.md

Step 1: Install DAHDI and Asterisk

**For users:**

```
cd /usr/src && wget https://docs.phreaknet.org/script/phreaknet.sh && chmod +x phreaknet.sh && ./phreaknet.sh make
phreaknet install --alsa -d -b -v 20 # -d for DAHDI, add -s for chan_sip (if you need it still)
```

The critical flags here are `--alsa`, which adds ALSA support to the build system (required for `chan_simpleusb` and `chan_usbradio` to build) and `-d`, to install DAHDI.

**For developers:**

Developers should build Asterisk with DEVMODE enabled (for backtraces and assertions) and also install the test suite:

```
cd /usr/src && wget https://docs.phreaknet.org/script/phreaknet.sh && chmod +x phreaknet.sh && ./phreaknet.sh make
phreaknet install --alsa --dahdi --devmode --testsuite
```

Step 2: Install app_rpt modules

- Clone this repo into `/usr/src` on your system: `cd /usr/src; git clone https://github.com/AllStarLink/app_rpt.git`

- If you would like to also install the test suite, also clone the test suite now: `cd /usr/src; git clone https://github.com/asterisk/testsuite.git`

- Finally, run: `./rpt_install.sh`. This compiles Asterisk with the radio modules and adds the radio tests to the test suite, if it was present.

### Running the tests

If you installed the test suite, you can run the `app_rpt` tests by running `cd /usr/src/testsuite; ./runInVenv.sh python3 runtests.py --test=tests/apps/rpt`

You can also use the `phreaknet runtest` command during development (e.g. `phreaknet runtest apps/rpt`), as it has some helpful tooling and wrappers to help with debugging when tests fails.

## Manual Installation (not recommended)

If you want to manually install app_rpt et al., here is how:

### Pre-Reqs

`chan_simpleusb` and `chan_usbradio` require `libusb-dev` on Debian:

`apt-get install -y libusb-dev`

### Compiling

First, detection of the ALSA library needs to be re-added to the build system, by applying the following patch: https://github.com/InterLinked1/phreakscript/blob/master/patches/alsa.diff

Then, add this near the bottom of `apps/Makefile`:

`$(call MOD_ADD_C,app_rpt,$(wildcard app_rpt/rpt_*.c))`

Add this near the bottom of `channels/Makefile`:

`chan_simpleusb.so: LIBS+=-lusb -lasound`

`chan_usbradio.so: LIBS+=-lusb -lasound`

### After DAHDI/Asterisk installed

`/dev/dsp1` needs to exist for chan_simpleusb and chan_usbradio to work.

This StackOverflow post contains the answer in an upvoted comment: https://unix.stackexchange.com/questions/103746/why-wont-linux-let-me-play-with-dev-dsp/103755#103755

Run: `modprobe snd-pcm-oss` (as root/sudo)
