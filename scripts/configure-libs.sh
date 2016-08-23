#!/bin/sh

cd libbson
if [ -f Makefile ]; then
	make distclean
fi
./autogen.sh --enable-decimal-bid=no --disable-experimental-features
cd -

cd libmongoc
if [ -f Makefile ]; then
	make distclean
fi
./autogen.sh --disable-experimental-features
cd -
