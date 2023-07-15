#!/bin/sh
# This script automates the steps for building app_rpt-enhanced asterisk deb packages 
# based off of the phreaknet-patched asterisk source.
# Currently this script is for devleoper convenience for creating the packages. 

set -e

# Go to directory of this script
cd $(dirname $0)

# Get the path to this repo
APP_RPT_PATH=`pwd`

##########################
# ENVIRONMENT SETUP
##########################

# Here are gathered the environment setup commands that need sudo
# These should only need to be run once on the build system.
# After that they can be commented out for subsequent runs, assuming dependencies don't change

# The build environment must have the following dependencies installed for this script to work
sudo apt -y install wget git subversion devscripts
# Install the debian packages that the build needs
sudo mk-build-deps --install --tool "apt-get -y -o Debug::pkgProblemResolver=yes --no-install-recommends"
# The asterisk build depends on the phreaknet-patched dahdi
wget https://docs.phreaknet.org/script/phreaknet.sh -O phreaknet.sh && chmod +x phreaknet.sh
sudo ./phreaknet.sh dahdi -f
# Remove manually installed libs from dahdi-tools that interfere with debian build
sudo rm -rf /usr/lib/libtonezone.*

##########################
# BEGIN SOURCE CODE SETUP
##########################

# Go to parent directory
cd ../
BUILD_PATH=`pwd`

# Get phreaknet script
wget https://docs.phreaknet.org/script/phreaknet.sh -O phreaknet.sh && chmod +x phreaknet.sh

# Use phreaknet to get asterisk source
cd $BUILD_PATH
./phreaknet.sh source -f

# cd into Asterisk source directory
cd $BUILD_PATH
AST_SOURCE_DIR=$(ls -d -v */ | grep "^asterisk" | tail -1)
cd $AST_SOURCE_DIR

# Copy app_rpt source into tree
for d in apps channels debian include configs res utils; do
    cp -r $APP_RPT_PATH/$d .
done

# Copy base config files
cp $APP_RPT_PATH/configs/rpt/* debian/ast_config/

# Apply the git patches for app_rpt
find $APP_RPT_PATH -name "*.diff" -exec git apply {} \;

##########################
# PERFORM DEBIAN BUILD
##########################

# Store the commit hash, to report in changelog
COMMIT=`git -C $APP_RPT_PATH log -1 --format=%h`

# Store email for use in dch command. Override this with DEBEMAIL and DEBFULLNAME in the environment
export NAME=$USER
export EMAIL=$USER@$HOSTNAME

# Update changelog
dch --append "Building app_rpt commit $COMMIT"

# Run the debian build, binary only
debuild -uc -us -b

