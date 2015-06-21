#!/bin/bash
BUILD_FOLDER = build
               CPUCOUNT = `awk '/processor/{i++}END{print i}' / proc / cpuinfo`
                          [ ! -d $BUILD_FOLDER ] && mkdir $BUILD_FOLDER
                          pushd $BUILD_FOLDER
                          qmake ..
                          make - j $CPUCOUNT
                          popd
