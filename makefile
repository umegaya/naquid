RELATIVE_PROJECT_ROOT=../..
BUILD_SETTING_PATH=$(RELATIVE_PROJECT_ROOT)/tools/cmake
CHROMIUM_ROOT=../chromium
BUILDER_IMAGE=naquid/meta-builder
LIB=nq
# osx/linux/windows
TEST_OS=osx
DEBUG=False
TEST_DEBUG=True
TEST_SVHOST=127.0.0.1
JOB=4
WINDOWS_GENERATOR=Visual Studio 15 2017

define ct_run
docker run --rm -v `pwd`:/naquid $(BUILDER_IMAGE) sh -c "cd /naquid && $1"
endef

meta-builder:
	docker build -t naquid/meta-builder tools/builder

builder:
	bash tools/builder/create.sh $(CHROMIUM_ROOT)

rebuild-builder: meta-builder builder

bundle:
	-@mkdir -p build/osx
	cd build/osx && cmake -DDEBUG:BOOLEAN=$(DEBUG) -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/bundle.cmake $(RELATIVE_PROJECT_ROOT) && make -j$(JOB)

testlib:
	-@mkdir -p build/t/$(TEST_OS)
	cd build/t/$(TEST_OS) && cmake -DDEBUG:BOOLEAN=$(TEST_DEBUG) -DCMAKE_TOOLCHAIN_FILE=../$(BUILD_SETTING_PATH)/$(TEST_OS).cmake $(RELATIVE_PROJECT_ROOT)/.. && make -j$(JOB)

linux_internal: 
	-@mkdir -p build/linux
	cd build/linux && cmake -DDEBUG:BOOLEAN=$(DEBUG) -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/linux.cmake $(RELATIVE_PROJECT_ROOT) && make -j$(JOB)

linux:
	$(call ct_run,make linux_internal)

linux_sh: 
	docker run --name nqsh --rm -ti --privileged --add-host test.qrpc.io:$(TEST_SVHOST) -v `pwd`:/naquid naquid/meta-builder bash || docker exec -ti nqsh bash

windows:
	-@mkdir -p build/windows
	cd build/windows && cmake -G '$(WINDOWS_GENERATOR)' -DDEBUG:BOOLEAN=$(DEBUG) -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/windows.cmake $(RELATIVE_PROJECT_ROOT)

ios:
	-@mkdir -p build/ios.v7
	-@mkdir -p build/ios.64
	-@mkdir -p build/ios
	cd build/ios.v7 && cmake -DDEBUG:BOOLEAN=$(DEBUG) -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_PLATFORM=iPhoneOS -DIOS_ARCH=armv7 $(RELATIVE_PROJECT_ROOT) && make -j$(JOB)
	cd build/ios.64 && cmake -DDEBUG:BOOLEAN=$(DEBUG) -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_PLATFORM=iPhoneOS -DIOS_ARCH=arm64 $(RELATIVE_PROJECT_ROOT) && make -j$(JOB)
	lipo build/ios.v7/lib$(LIB).a build/ios.64/lib$(LIB).a -create -output build/ios/lib$(LIB).a
	strip -S build/ios/lib$(LIB).a

android:
	-@mkdir -p build/android.v7
	-@mkdir -p build/android.64
	-@mkdir -p build/android
	cd build/android.v7 && cmake -DDEBUG:BOOLEAN=$(DEBUG) -DNQ_ANDROID:BOOLEAN=True -DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake -DANDROID_ABI="armeabi-v7a" -DANDROID_NATIVE_API_LEVEL=android-16 -DANDROID_STL=c++_static $(RELATIVE_PROJECT_ROOT) && make -j$(JOB)
	cd build/android.64 && cmake -DDEBUG:BOOLEAN=$(DEBUG) -DNQ_ANDROID:BOOLEAN=True -DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake -DANDROID_ABI="arm64-v8a" -DANDROID_NATIVE_API_LEVEL=android-16 -DANDROID_STL=c++_static $(RELATIVE_PROJECT_ROOT) && make -j$(JOB)
	mv build/android.v7/lib$(LIB).so build/android/lib$(LIB)-armv7.so
	mv build/android.64/lib$(LIB).so build/android/lib$(LIB)-arm64.so

inject:
	python tools/deps/tools/sync.py --dir ./src --dir $(CHROMIUM_ROOT) \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/protobuf/src/google \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/boringssl/src/crypto \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/boringssl/src/include \
		--sync_dir=$(CHROMIUM_ROOT)/third_party/zlib \
		--sync_dir=$(CHROMIUM_ROOT)/url \
		--altroot_file=./tools/deps/tools/altroots.list \
		--outdir=./src/chromium ./src/nq.cpp 

patch:
	cd ./src/chromium && patch -p1 < ../../tools/patch/current.patch

sync: inject patch

testsv:
	make -C test/e2e server TEST_OS=$(TEST_OS) DEBUG=$(TEST_DEBUG)

testcl:
	make -C test/e2e client TEST_OS=$(TEST_OS) DEBUG=$(TEST_DEBUG)

testclean:
	-@rm -r build/t/$(TEST_OS)
	make -C test/e2e clean TEST_OS=$(TEST_OS) DEBUG=$(TEST_DEBUG)

.PHONY: test
test: testlib
	make -C test/e2e test TEST_OS=$(TEST_OS) DEBUG=$(TEST_DEBUG)
