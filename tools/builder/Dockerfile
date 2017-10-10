FROM alpine:3.1

RUN apk add --update git build-base autoconf automake libtool libstdc++
RUN bash -c "curl https://cmake.org/files/v3.9/cmake-3.9.4.tar.gz | tar -zxv" && \ 
	cd cmake-3.9.4 && ./bootstrap && make && make install && \
	cd / && rm -r cmake-3.9.4
