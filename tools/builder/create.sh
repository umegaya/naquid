#!/bin/bash

PWD=`pwd`
echo $PWD

rm ${PWD}/tools/builder/finish

set -e

if [ -z $1 ]; then
	echo "usage: ./create.sh ${RELATIVE_CHROMIUM_ROOT_PATH}"
	exit 1
fi
REL_CHROMIUM_ROOT=$1

echo "run builder container"
CID=`docker run -d \
	-v ${PWD}/tools/builder:/builder \
	-v ${PWD}/${REL_CHROMIUM_ROOT}/third_party/protobuf:/protobuf naquid/meta-builder \
	sh -c "cd /protobuf && ./autogen.sh && ./configure && make -j3 && make install && touch /builder/finish && sleep 60"`
FINISH_FILE=${PWD}/tools/builder/finish
echo "wait builder $CID finish at $FINISH_FILE"
while [ ! -f $FINISH_FILE ]; do
	printf "."
	sleep 5
done
docker commit $CID naquid/builder
docker kill $CID
docker rm $CID
echo "finish"