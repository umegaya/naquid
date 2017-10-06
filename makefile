QUIC_CORE_PROTO_ROOT=ext/chromium/net/quic/core/proto
BUILD_SETTING_PATH=tools/cmake
RELATIVE_PROJECT_ROOT=../..
QUIC_CORE_PROTO_SRC=$(shell find $(QUIC_CORE_PROTO_ROOT) -name *.proto)
BUILDER_IMAGE=barequic/builder

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
	cd build/osx && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/bundle.cmake $(RELATIVE_PROJECT_ROOT) && make -j4

sync:
	python tools/deps/inject.py --dir ./src --dir ./ext/chromium --debug --exclude_file=./tools/deps/excludes.list --outdir=tools/deps src/naquid.cpp 
	python tools/deps/inject_ssl.py ./ext/chromium/third_party/boringssl/src/crypto tools/deps/ssl.cmake