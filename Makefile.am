#  
#   This file is part of Sylverant PSO Server.
#  
#   Copyright (C) 2009 Lawrence Sebald
#  
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License version 3 as
#   published by the Free Software Foundation.
#  
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#  
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

ACLOCAL_AMFLAGS = -I m4

LIBS += $(PTHREAD_LIBS)
AM_CFLAGS = $(PTHREAD_CFLAGS)

bin_PROGRAMS = shipgate
shipgate_SOURCES = src/packets.c src/ship.c src/ship.h src/ship_packets.h \
                   src/shipgate.c src/shipgate.h

datarootdir = @datarootdir@
