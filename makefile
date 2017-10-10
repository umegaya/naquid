BUILD_SETTING_PATH=tools/cmake
RELATIVE_PROJECT_ROOT=../..
CHROMIUM_ROOT=../chromium
LIB=nq

define ct_run
docker run --rm -v `pwd`:/naquid $(BUILDER_IMAGE) bash -c "cd /naquid && $1"
endef

meta-builder:
	docker build -t naquid/meta-builder tools/builder

builder:
	bash tools/builder/create.sh $(CHROMIUM_ROOT)

rebuild-builder: meta-builder builder

bundle:
	-@mkdir -p build/osx
	cd build/osx && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/bundle.cmake $(RELATIVE_PROJECT_ROOT) && make

linux_internal: 
	- mkdir -p build/linux
	cd build/linux && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/linux.cmake $(RELATIVE_PROJECT_ROOT) && make

linux:
	$(call ct_run,make linux_internal)

ios:
	- mkdir -p build/ios.v7
	- mkdir -p build/ios.64
	- mkdir -p build/ios
	cd build/ios.v7 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_PLATFORM=iPhoneOS -DIOS_ARCH=armv7 $(RELATIVE_PROJECT_ROOT) && make
	cd build/ios.64 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_PLATFORM=iPhoneOS -DIOS_ARCH=arm64 $(RELATIVE_PROJECT_ROOT) && make
	lipo build/ios.v7/lib$(LIB).a build/ios.64/lib$(LIB).a -create -output build/ios/lib$(LIB).a
	strip -S build/ios/lib$(LIB).a

android:
	- mkdir -p build/android.v7
	- mkdir -p build/android.64
	- mkdir -p build/android
	cd build/android.v7 && cmake -DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake -DANDROID_ABI="armeabi-v7a" -DANDROID_NATIVE_API_LEVEL=android-16 -DANDROID_STL=c++_static $(RELATIVE_PROJECT_ROOT) && make -j4
	cd build/android.64 && cmake -DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake -DANDROID_ABI="arm64-v8a" -DANDROID_NATIVE_API_LEVEL=android-16 -DANDROID_STL=c++_static $(RELATIVE_PROJECT_ROOT) && make -j4
	mv build/android.v7/lib$(LIB).so build/android/lib$(LIB)-armv7.so
	mv build/android.64/lib$(LIB).so build/android/lib$(LIB)-arm64.so

sync:
	python tools/deps/tools/sync.py --dir ./src --dir $(CHROMIUM_ROOT) \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/protobuf/src/google \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/boringssl/src/crypto \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/boringssl/src/include \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/zlib \
		--sync_dir=$(CHROMIUM_ROOT)/url \
		--altroot_file=./tools/deps/tools/altroots.list \
		--outdir=./src/chromium ./src/naquid.cpp 

patch:
	cd ./src/chromium && patch -p1 < ../../tools/patch/current.patch
