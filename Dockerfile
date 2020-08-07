FROM alpine:3.10

LABEL maintainer="Peter Weiss" description="Home automation gateway for KNX, HTTP(S), plain TCP, MQTT and serial devices"

RUN set -x && \
	apk --no-cache add \
		libgcc \
		libstdc++ \
		mosquitto-libs \
		libcurl \
		tzdata && \
	apk --no-cache add --virtual build-deps \
		build-base \
		cmake \
		git \
		mosquitto-dev \
		rapidjson-dev \
		curl-dev \
		util-linux-dev && \
	mkdir -p /build && \
	cd /build && \
	git clone https://github.com/peti69/weaver && \
	cd /build/weaver && \
	cmake . && \
	make && \
	mkdir -p /weaver/conf /weaver/log && \
	install -d /usr/bin && \
	install -s -m755 /build/weaver/src/weaver /usr/bin/weaver && \
	install -m644 /build/weaver/conf/weaver.conf /weaver/conf && \
	install -m644 /build/weaver/conf/roomba.cert /weaver/conf && \
	apk del build-deps && \
	rm -rf /build

ENV TZ Europe/Berlin

VOLUME ["/weaver/conf", "/weaver/log"]

WORKDIR /weaver

CMD ["/usr/bin/weaver", "conf/weaver_conf.json"]
