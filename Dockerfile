FROM debian:latest

RUN apt-get update && \
    apt-get install -y logrotate libmosquitto1 gcc-multilib && \
	rm -rf /var/lib/apt/lists/* && \
	mkdir -p /weaver/conf && \
	mkdir -p /weaver/log

VOLUME ["/weaver/conf", "/weaver/log"]

COPY weaver /usr/bin
COPY weaver_conf.json /weaver/conf

#ENV PATH="/usr/bin:${PATH}"

CMD ["/usr/bin/weaver", "/weaver/conf/weaver_conf.json"]
