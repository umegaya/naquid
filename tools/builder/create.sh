#!/bin/bash

PWD=`pwd`
echo $PWD

set -e

rm ${PWD}/tools/builder/finish
echo "run builder container"
CID=`docker run -d \
	-v ${PWD}/tools/builder:/builder \
	-v ${PWD}/ext/chromium/third_party/protobuf:/protobuf barequic/meta-builder \
	sh -c "cd /protobuf && ./autogen.sh && ./configure && make -j3 && make install && touch /builder/finish && sleep 60"`
FINISH_FILE=${PWD}/tools/builder/finish
echo "wait builder $CID finish at $FINISH_FILE"
while [ ! -f $FINISH_FILE ]; do
	printf "."
	sleep 5
done
docker commit $CID barequic/builder
docker kill $CID
docker rm $CID
echo "finish"