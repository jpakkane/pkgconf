#! /bin/sh
# Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer
#    in this position and unchanged.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Install build dependencies for pkg on Mac or Linux (Debiaun/Ubuntu) systems.
#
# This script is primarily intended for travis CI to be able to setup the
# build environment.
#
# To run on a clean Ubuntu system, try something like this:
#  export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
#  sudo apt install liblua5.2-dev libsqlite3-dev
#  sh -ex scripts/install_deps.sh 

install_from_github() {
	local name="${1}"
	local ver="${2}"
	local distname="${name}-${ver}"

	# https://github.com/jmmv/kyua/releases/download/kyua-0.12/kyua-0.12.tar.gz
	local url="https://github.com/jmmv/${name}"
	wget "${url}/releases/download/${distname}/${distname}.tar.gz"
	tar -xzvf "${distname}.tar.gz"

	cd "${distname}"
	./configure \
		--disable-developer \
		--without-atf \
		--without-doxygen \
		CPPFLAGS="-I/usr/local/include" \
		LDFLAGS="-L/usr/local/lib -Wl,-R/usr/local/lib" \
		PKG_CONFIG_PATH="/usr/local/lib/pkgconfig"
	make -j2
	$SUDO make install
	cd -

	rm -rf "${distname}" "${distname}.tar.gz"
}

SUDO=sudo
CFLAGS="-g -O2"
case $(uname -s) in
Darwin)
	brew update
	brew install libarchive --with-xz
	brew install openssl
	brew install kyua
	;;
Linux)
	install_from_github atf 0.21
	install_from_github lutok 0.4
	install_from_github kyua 0.12
	;;
CYGWIN*)
	SUDO=""
	apt-cyg update
	apt-cyg install lua-devel libsqlite3-devel
	install_from_github lutok 0.4

	# Work around
	# utils/process/system.cpp:59:68: error: invalid conversion from ‘pid_t (*)(pid_t, __wait_status_ptr_t, int) {aka int (*)(int, void*, int)}’ to ‘pid_t (*)(pid_t, int*, int) {aka int (*)(int, int*, int)}’ [-fpermissive]
	# pid_t (*detail::syscall_waitpid)(const pid_t, int*, const int) = ::waitpid;
	#								    ^
	# make[1]: *** [Makefile:7014: utils/process/libutils_a-system.o] Error 1
	export CXXFLAGS="-g -O2 -fpermissive"
	install_from_github kyua 0.12
	unset CXXFLAGS

	install_from_github atf 0.21
	;;
esac
