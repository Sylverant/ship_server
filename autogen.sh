#!/bin/sh
libtoolize --install --copy && aclocal && autoconf && automake --add-missing --copy

