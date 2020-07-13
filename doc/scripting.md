# Sylverant Server-side Scripting with Lua

Last update: July 12, 2020

***

## Introduction

Server-side scripting functionality is an optional feature that can be built-in
to both the Ship Server and Shipgate of the Sylverant PSO Server. These scripts
can be used to respond to various events that occur in-game while users are
playing on the server in order to extend the functionality of the server. The
main designed use of this functionality is for implementing special events that
track things like enemy kills, but it could also be used for many other things.

The scripting functionality is based around the [Lua](https://lua.org) scripting
language, specifically version 5.3 or newer. It is possible to import libraries
that are in the default Lua search path, as well as the scripts subdirectory of
your server installation.

The Ship server and Shipgate each provide various events that can be responded
to by scripts. In addition both Ship Server and Shipgate add additional library
functionality to the Lua environment that can be used to interact with the
running instance of the server. Most of the rest of this documentation will
describe the events that can be responded to and the library functionality that
is provided to scripts.

It is important to note that Shipgate has the ability to send scripts to the
connected Ship Server instances to have them run the scripts on various events.
This is exactly how server-side kill tracking events have been implemented on
Sylverant since the scripting functionality was added. However, scripts provided
by the Shipgate do not override any scripts that have been configured on the
ship. That is to say that you can have a script configured locally for the same
event as a script that is sent by the Shipgate and both scripts will run. This
functionality may be tweaked at a later date to make it so that
Shipgate-provided scripts can override locally configured scripts selectively.

## Scriptable Events in Ship Server

In general, scripted events should return a non-zero value if the script acted
on the event in question, or a zero value if the script did respond or could not
respond. In some cases the return value is ignored, but it is good practice to
follow the above guidelines.

Each script is provided with some number of arguments to give the script context
as to what event has occurred. These arguments can be accessed in the Lua script
as shown below (the example shown here is taken from an unknown chat command
script):

```lua
local args = { ... }
local c = args[1]
local cmd = args[2]
local params = args[3]
```

The list of scriptable events in the Ship Server and the arguments they take
are as follows (shown as a C-like declaration to make it clear what types the
arguments are):

* `ShipStartup(ship_t *ship)`
* `ShipShutdown(ship_t *ship)`
* `ShipClientLogin(ship_client_t *c)`
* `ShipClientLogout(ship_client_t *c)`
* `BlockClientLogin(ship_client_t *c)`
* `BlockClientLogout(ship_client_t *c)`
* `UnknownShipPacket(ship_cient_t *c, lua_string pkt)`
* `UnknownBlockPacket(ship_client_t *c, lua_string pkt)`
* `UnknownEpisode3Packet(ship_client_t *c, lua_string pkt)`
* `TeamCreate(ship_client_t *c, lobby_t *l)`
* `TeamDestroy(lobby_t *l)`
* `TeamJoin(ship_cient_t *c, lobby_t *l)`
* `TeamLeave(ship_client_t *c, lobby_t *l)`
* `EnemyKill(ship_client_t *c, uint16_t enemy_id)`
* `EnemyHit(ship_client_t *c, uint16_t enemy_id)`
* `BoxBreak(ship_client_t *c, uint16_t object_id, uint16_t object_type)`
* `UnknownCommand(ship_client_t *c, lua_string cmd, lua_string args)`
* `ShipgateData(ship_client_t *c, uint32_t event_id, lua_string data)`
* `UnknownMenuSelection(ship_client_t *c, uint32_t menu_id, uint32_t item_id)`
* `BankAction(ship_client_t *c, int take, uint32_t item_data1,
uint32_t item_data2, uint32_t item_data3, uint32_t item_data4,
uint32_t item_id)`
* `ChangeArea(ship_client_t *c, int new_area, int old_area)`
* `QuestSyncRegister(ship_client_t *c, lobby_t *l, uint8_t reg_num,
uint32_t value)`

## Scriptable Events in Shipgate

The ability to script Shipgate is much more limited than what is available in
Ship Server. In actuality, there is only one scriptable event that is available
in Shipgate currently, which is to respond to data sent by the Ship Server:

* `ShipData(ship_t *s, uint32_t ship_id, uint32_t block, uint32_t event_id,
uint32_t guildcard, uint8_t episode, uint8_t difficulty, uint8_t version,
lua_string data)`

## Library Support in Ship Server

Various parts of the Ship Server have functionality that Lua scripts can
interact with. This functionality is often expanded, so please be sure to always
refer to the most recent version of this document to ensure that you have the
most recent information.

Specifically, Ship Server currently adds Lua libraries for interacting with
clients, the ship itself, and lobbies (usually teams, but teams are considered
to be a kind of lobby by the Ship Server software for various reasons that are
way outside of the scope of this document). These libraries are automatically
available in any Lua script registered through one of the events listed up
above, meaning that you do not have to add any `import` statements to your
scripts to use them.

Each library registers a number of different functions that are made available
to the script. These libraries do not (currently) provide any sort of Lua
objects to the scripts, and are interacted with in a very simple manner, as
shown in the example below, which calls the `writeLog` function of the ship
Lua library to write to the ship's log file:

```lua
ship.writeLog("Hello, ship log file!")
```

Each of the functions of the libraries made available to Lua scripts running in
Ship Server are described in the following few sections.

### Ship Lua Library

The `ship` library provides functionality for interacting with the top level
ship. Basically, this provides some simple global functionality and not much
else. The functions provided by this library are detailed below (with C-like
fucntion definitions for clarity). In any case where a `ship_t *` value is
required, this is a Lua light userdata value which points to the ship structure
as used in the rest of the server. This is often retrieved with a function in
the `client` library.

* `lua_string ship.name(ship_t *s)`: Retrieve the name of the ship, as specified
in its configuration file.
* `lua_table ship.getTable(ship_t *s)`: Retrieves a table which is used to hold
global values for the ship.
* `void ship.writeLog(lua_string str)`: Writes the specified string to the
ship's log, following the normal formatting for log messages. This message is
written with a `DBG_LOG` verbosity level.

### Lobby (Team) Lua Library

The `lobby` library provides functionality for interacting with a team of
players. As with the `ship` library, you will need to obtain a light userdata
reference to the `lobby_t *` that is associated with the team in order to use
the functions listed below.

* `lua_Integer lobby.id(lobby_t *l)`: Retrieve the unsigned 32-bit ID that the
server has assigned to this lobby.
* `lua_Integer lobby.type(lobby_t *l)`: Retrieve the kind of lobby that this
lobby structure represents. This should have one of the following values:
    * 1 For a visual lobby.
    * 2 For a normal team.
    * 4 For an Episode 3 team.
* `lua_Integer lobby.flags(lobby_t *l)`: Retrieve the internal flags value
associated with this lobby. This is a bitfield of `LOBBY_FLAG_*` values as
defined in the lobby.h file of the Ship Server.
* `lua_Integer lobby.numClients(lobby_t *l)`: Retrieve the number of clients
currently in the specified lobby/team.
* `lua_Integer lobby.num_clients(lobby_t *l)`: Same as above, but the old name
for it.
* `lua_Integer lobby.block(lobby_t *l)`: Retrieve the number of the block that
this lobby/team is on.
* `lua_Integer lobby.version(lobby_t *l)`: Retrieve which version of the game
that this lobby/team is for. In general this will be the lowest version of the
game that this lobby will allow to play in it. See clients.h in the Ship Server
source code for the list of versions (look for `CLIENT_VERSION_*`).
* `lua_Integer lobby.leaderID(lobby_t *l)`: Retrieve the internal ID associated
with the current leader of the team. This is just an index into the array of
clients that represents the client that determines drops and other such
bookkeeping functionality.
* `ship_client_t *lobby.leader(lobby_t *l)`: Retrieve a light userdata reference
to the client structure of the leader of the team, as defined above.
* `lua_Integer lobby.difficulty(lobby_t *l)`: Retrieve the difficulty level of
the specified team.
* `lua_Boolean lobby.isBattleMode(lobby_t *l)`: Is this team a battle mode team?
* `lua_Boolean lobby.isChallengeMode(lobby_t *l)`: Is this team a challenge mode
team?
* `lua_Integer lobby.section(lobby_t *l)`: Retrieve the section ID of the client
who created this team. This value is not updated after team creation and may not
represent the current set of drops, depending on if the team leader has ever
changed.
* `lua_Integer lobby.episode(lobby_t *l)`: Retrieve the episode number of PSO
that the team is playing, assuming it is a normal team. This value is updated
if a quest is loaded that changes the episode number.
* `lua_string lobby.name(lobby_t *l)`: Retrieve the name of the team.
* `lua_Integer lobby.questID(lobby_t *l)`: Retrieve the ID of the quest that is
currently being played in the team if any. If no quest is in progress, this will
return 0.
* `ship_client_t *lobby.client(lobby_t *l, int id)`: Retrieve a light userdata
reference to the client who has the ID specified, if one exists. Returns `nil`
if the ID is not valid for the current lobby or team.
* `lua_table lobby.clients(lobby_t *l)`: Retrieve a table containing light
userdata references to all clients in the specified lobby or team.
* `lua_Integer lobby.sendMsg(lobby_t *l, lua_string msg)`: Send the specified
message to all clients in the team. This message will appear about midway down
the screen on the right hand side (the same place global messages appear), and
will have the color set to white to ensure that the player can see it. This
function always returns 0.
* `lua_table lobby.getTable(lobby_t *l)`: Retrieve a Lua table that can be used
to store any sort of team/lobby specific data that is useful to scripts. Please
note that unless you remove the information from the table later, it will stay
in memory until the lobby or team is deallocated by the server.
* `lua_Integer lobby.randInt(lobby_t *l)`: Get a random 32-bit unsigned integer
value from the lobby/team's random number generator.
* `lua_Number lobby.randFloat(lobby_t *l)`: Get a random floating point value
from the lobby/team's random number generator. This value will be between 0 and
1, inclusive.
* `lua_Boolean lobby.setSinglePlayer(lobby_t *l)`: Sets the single-player mode
flag on the specified team if it is valid (that is to say if the team only has
one player in it still). Returns `true` if the call succeeded or `false`
otherwise.

### Client Lua Library

The `client` library provides functionality for interacting with a player within
the Ship Server. As with the `ship` library, you will need to obtain a light
userdata reference to the `ship_client_t *` that is associated with the client
in order to use the functions listed below (most events will give you this
reference in their argument list).

* `lua_Integer client.guildcard(ship_client_t *c)`: Retrieve the guild card
number of the specified client.
* `lua_Boolean client.isOnBlock(ship_client_t *c)`: Returns `true` if the client
is on a block, `false` otherwise.
* `void client.disconnect(ship_client_t *c)`: Disconnect the specified client
from the server most unceremoniously.
* `lua_string client.addr(ship_client_t *c)`: Retrieve the IP address that the
specified client is connecting from.
* `lua_Integer client.version(ship_client_t *c)`: Retrieve the version of the
game that the specified client is using. See the clients.h file in the Ship
Server source code for the values.
* `lua_Integer client.clientID(ship_client_t *c)`: Retrieve the internal ID
(index within the lobby/team) that the specified client has been assigned.
* `lua_Integer client.privilege(ship_client_t *c)`: Retrieve the privilege level
of the specified client. This is a bitfield. See the `CLIENT_PRIV_*` values in
clients.h for what these values mean.
* `lua_Integer client.send(ship_client_t *c, lua_string pkt)`: Send the
specified client a raw packet over the connection. Returns -1 on failure or 0
on success. **BE VERY CAREFUL USING THIS FUNCTION.**
* `lobby_t *client.lobby(ship_client_t *c)`: Retrieve a light userdata reference
to the lobby or team that the client is in.
* `lua_Integer client.block(ship_client_t *c)`: Retrieve a light userdata
reference to the block that the client is connected to.
* `lua_Integer client.sendScriptData(ship_client_t *c, uint32_t event,
lua_string data)`: Send script data to the Shipgate on behalf of the specified
client. This will result in the Shipgate calling the specified script handler.
* `lua_Integer client.sendMsg(ship_client_t *c, lua_string msg)`: Send the
specified user a message that will appear just as a global message does to the
player. The text will have its color set to white.
* `lua_table client.getTable(ship_client_t *c)`: Retrieve a Lua table that is
associated with the specified client that can be used to store any useful data
for scripts. Unless you remove the information from the table, it will be
resident in memory until the client is deallocated (that is to say until the
client disconnects from the server).
* `lua_Integer client.area(ship_client_t *c)`: Retrieve the index of the area
that the client is currently in, assuming that the client is in a team.
* `lua_string client.name(ship_client_t *c)`: Retrieve the name of the character
that the client is currently playing as.
* `lua_Integer client.flags(ship_client_t *c)`: Retrieve the flag bitfield
associated with the specified client. See the `CLIENT_FLAG_*` values in the
clients.h file in Ship Server for more information on these flags.
* `lua_Integer client.level(ship_client_t *c)`: Retrieve the level of the
character that the client is currently playing as. This is a value from 1-200.
* `lua_Integer client.sendMenu(ship_client_t *c, uint32_t menu_id, uint32_t
count, lua_table menu_data)`: Send a menu to the client from which the user can
pick an option. This uses the information window menu.
* `lua_Integer client.dropItem(ship_client_t *c, uint32_t item_data1, uint32_t
item_data2, uint32_t item_data3, uint32_t item_data4)`: Causes an item box to
be dropped at the specified client's current position containing the item
specified. All values after `item_data1` are optional and do not have to be
provided (however, to specify a value for a later data point you must specify
all of the ones before it). Unspecified values will default to 0.
* `lua_Integer client.sendMsgBox(ship_client_t *c, lua_string msg)`: Sends a
message to the client that will appear like the team information display on the
team selection menu. This is useful when responding to menu selections.
* `lua_Integer client.numItems(ship_client_t *c)`: Retrieve the number of items
in the specified client's pack when they first connected to the team. This value
is not (currently) updated for any items picked up, dropped, etc.
* `lua_table client.item(ship_client_t *c, int index)`: Retrieve the item data
values of the item at the specified index in the client's pack when the client
first connected to the team. This value is not (currently) updated for any items
picked up, dropped, etc.
* `ship_client_t *client.find(uint32_t gc)`: Retrieve a light userdata reference
to the client with the specified guild card number if they are currently
connected to this ship.
* `lua_Integer client.syncRegister(ship_client_t *c, uint8_t reg, uint32_t
value)`: Send a quest register sync packet to the client with the specified
register and value. This should only be called if a quest is currently
loaded in the team.
* `lua_Boolean client.hasItem(ship_client_t *c, uint32_t item_data1)`: Returns
`true` if the client has at least one of the specified item in their pack (as of
when they connected to the team).
* `lua_Boolean client.legitCheck(ship_client_t *c)`: Performs a "legit check"
of the specified client with the default limits file of the ship. Returns
`true` if the user passed the legit check. As with all other functions that
access inventory data, this only (currently) reflects the state of the client's
pack when they connected to the team.
* `lua_Boolean client.legitCheckItem(ship_client_t *c, uint32_t item_data1,
uint32_t item_data2, uint32_t item_data3, uint32_t item_data4)`: Perform a
"legit check" of the specified item data using the default limits file of the
ship. Returns `true` if it passes the check.
* `lua_table client.coords(ship_client_t *c)`: Retrieve the current position
(as best the server knows) of the specified client. The returned table will have
the X coordinate in the first index, the Y coordinate in the second index,
and the Z coordinate in the third (and final) index. These will all be floating
point values.
* `lua_Number client.distance(ship_client_t *c1, ship_client_t *c2)`: Calculate
the Euclidian distance between the specified clients, assuming they are both in
the same area. Returns -1 if the two clients are not in the same area.

## Library Support in Shipgate

There is only one library of additional functionality added to the Lua
environment in Shipgate, specifically the `shipgate` library. The Shipgate
itself is a lot less scriptable than the Ship Server, and the functionality
provided demonstrates this. The functions provided by this library are given
below.

* `void shipgate.writeLog(lua_string str)`: Writes the specified string to the
Shipgate's log with a verbosity level of `DBG_LOG`.
* `lua_Integer shipgate.sendScriptData(ship_t *s, uint32_t event,
uint32_t guildcard, uint32_t block, lua_string data)`: Send a script data packet
to the specified ship with the parameters given. As this will only generally be
done in response to a `ShipData` event, all of the parameters should come from
that event's information.
