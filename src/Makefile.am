#
#   This file is part of Sylverant PSO Server.
#
#   Copyright (C) 2009, 2010, 2011, 2012, 2013, 2018, 2019, 2021 Lawrence Sebald
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU Affero General Public License version 3
#   as published by the Free Software Foundation.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU Affero General Public License for more details.
#
#   You should have received a copy of the GNU Affero General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

BUILT_SOURCES =
CLEANFILES =
include $(top_srcdir)/git_version.am

LIBS += $(PTHREAD_LIBS) $(LIBICONV)
AM_CFLAGS = $(PTHREAD_CFLAGS)

bin_PROGRAMS = ship_server
ship_server_SOURCES = block.c block.h clients.c clients.h \
                      commands.c commands.h gm.c gm.h \
                      lobby.c lobby.h player.h ship.c \
                      ship.h ship_packets.c ship_packets.h \
                      ship_server.c shipgate.c shipgate.h \
                      utils.c utils.h subcmd.c subcmd.h \
                      list.c items.c items.h word_select.c \
                      word_select.h word_select-dc.h \
                      word_select-pc.h word_select-gc.h \
                      quests.c quests.h bans.c bans.h \
                      scripts.h scripts.c admin.h admin.c \
                      mapdata.h mapdata.c ptdata.h ptdata.c \
                      pmtdata.h pmtdata.c rtdata.h rtdata.c \
                      subcmd-dcnte.c quest_functions.h packets.h \
                      quest_functions.c smutdata.h smutdata.c

nodist_ship_server_SOURCES = version.h
EXTRA_ship_server_SOURCES = pidfile.c flopen.c

if NEED_PIDFILE
AM_CFLAGS += -DNEED_PIDFILE=1
ship_server_DEPENDENCIES = pidfile.c flopen.c
endif

datarootdir = @datarootdir@
