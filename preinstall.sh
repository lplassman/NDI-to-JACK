#!/usr/bin/env sh

apt-get update

#install prerequisites
apt-get -y install --no-install-recommends \
	build-essential \
	libjack-jackd2-dev \
	qjackctl \
	jackd \	
    avahi-daemon \
    avahi-discover \
    avahi-utils \
    libssl-dev \
    libconfig++-dev \
	g++ \	
    curl \
    || exit 1

