#!/bin/bash

source $(dirname $0)/../zmk/.venv/bin/activate
set -x 

DIR=$(cd $(dirname $0); pwd)

cd $(dirname $0)/../zmk/app

west build -p auto -d $DIR/build -b agentickbd -S studio-rpc-usb-uart $@ -- -DZMK_CONFIG="$DIR/config" -DZMK_EXTRA_MODULES="$DIR;$DIR/../zmk-raw-hid" -DCONFIG_ZMK_STUDIO=y -DSHIELD=raw_hid_adapter
