#!/bin/bash

./waf clean

# ./waf configure --disable-python --enable-examples -d optimized
./waf configure --disable-python --enable-examples -d debug

./waf
