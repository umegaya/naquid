QUIC_CORE_PROTO_ROOT=ext/chromium/net/quic/core/proto
BUILD_SETTING_PATH=tools/cmake
RELATIVE_PROJECT_ROOT=../..
QUIC_CORE_PROTO_SRC=$(shell find $(QUIC_CORE_PROTO_ROOT) -name *.proto)
BUILDER_IMAGE=barequic/builder
CHROMIUM_ROOT=../chromium

define call_protoc
docker run --rm -v `pwd`:/barequic $(BUILDER_IMAGE) bash -c "cd /barequic && protoc -I$(QUIC_CORE_PROTO_ROOT) $1"
endef

$(QUIC_CORE_PROTO_ROOT)/%.pb.cc $(QUIC_CORE_PROTO_ROOT)/%.pb.h: $(QUIC_CORE_PROTO_ROOT)/%.proto 
	$(call call_protoc,-I. --cpp_out=$(QUIC_CORE_PROTO_ROOT) $<)

proto: $(QUIC_CORE_PROTO_SRC:.proto=.pb.cc)

meta-builder:
	docker build -t barequic/meta-builder tools/builder

builder: meta-builder
	bash tools/builder/create.sh

bundle: proto 
	-@mkdir -p build/osx
	cd build/osx && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/bundle.cmake $(RELATIVE_PROJECT_ROOT) && make

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
