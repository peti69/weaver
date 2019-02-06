FROM alpine:3.8

LABEL maintainer="Peter Weiss" description="Home automation gateway for KNX, MQTT and serial devices"

RUN set -x && \
    apk --no-cache add \
		libgcc \
		libstdc++ \
		mosquitto-libs \
		tzdata && \
    apk --no-cache add --virtual build-deps \
        build-base \
        cmake \
		git \
		mosquitto-dev \
		rapidjson-dev \
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
	install -m644 /build/weaver/weaver_conf.json /weaver/conf && \
	apk del build-deps && \
	rm -rf /build

ENV TZ Europe/Berlin

VOLUME ["/weaver/conf", "/weaver/log"]

CMD ["/usr/bin/weaver", "/weaver/conf/weaver_conf.json"]
