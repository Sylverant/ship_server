/*
    Sylverant Ship Server
    Copyright (C) 2009, 2011 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License version 3
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef COMMANDS_H
#define COMMANDS_H

#include "ship_packets.h"

int command_parse(ship_client_t *c, dc_chat_pkt *pkt);
int wcommand_parse(ship_client_t *c, dc_chat_pkt *pkt);
int bbcommand_parse(ship_client_t *c, bb_chat_pkt *pkt);

#endif /* COMMANDS_H */
