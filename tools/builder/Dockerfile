FROM ubuntu:16.10

RUN apt-get update && \
	apt-get install -y zlib1g-dev libssl1.0.0 gcc g++ cmake git pkg-config build-essential autoconf libtool gdb curl unzip && \
	rm -rf /var/lib/apt/lists/*
RUN bash -c "curl https://cmake.org/files/v3.9/cmake-3.9.4-Linux-x86_64.sh > ./install.sh" && \ 
	bash -c "printf 'y\ny\n' | bash ./install.sh" && \
	mv cmake-3.9.4-Linux-x86_64/bin/* /usr/local/bin && \
	mv cmake-3.9.4-Linux-x86_64/share/* /usr/local/share && \
	cd / && rm -r cmake-3.9.4-Linux-x86_64