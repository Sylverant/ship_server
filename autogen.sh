#!/bin/sh
libtoolize --install --copy && aclocal && autoheader && autoconf && automake --add-missing --copy

