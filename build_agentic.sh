#!/bin/bash

source $(dirname $0)/../zmk/.venv/bin/activate
set -x 

DIR=$(cd $(dirname $0); pwd)

cd $(dirname $0)/../zmk/app

west build -d $DIR/build -b agentickbd -S studio-rpc-usb-uart $@ -- -DZMK_CONFIG="$DIR/config" -DZMK_EXTRA_MODULES="$DIR" -DCONFIG_ZMK_STUDIO=y
