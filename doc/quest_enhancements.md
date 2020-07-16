# Sylverant Server-side Quest Enhancements

Last update: July 15, 2020

***

## Introduction

Quests in Phantasy Star Online are completely run client-side. That is to say
that once a client selects a quest from the server and the server sends it to
the client, the server has no more role (normally) in running the quest beyond
the normal functionality it provides in forwarding packets back and forth
between clients. Sylverant can, through its normal Lua scripting functionality,
take a more active role in quests by monitoring register synchronization packets
and acting on them in a Lua script. However, for many things this functionality
is pretty cumbersome and has its limits. Sylverant also provides two additional
ways for the server to enhance quests beyond using Lua scripts.

The first of these two is the ability for quest scripts to, once again through
the register synchronization functionality, save small pieces of data on the
server. These "server-side flags" are saved on a per guild card basis on the
Shipgate and can be read back to a quest through register synchronization
packets. These server-side flags are limited to 16-bit values because of the
(somewhat convoluted) process of saving and reading them in quests.

The second enhancement method provided by Sylverant is an idea of server-side
quest functions. Each successive version of PSO provided more functionality for
quests to make use of, and the idea here was originally to add in some subset of
the added functionality for older versions of PSO. The idea has grown since
then, but the guiding principle still is the same: add in additional
functionality for quests to use without too much extra trouble. Like the
server-side flags support described briefly above, this is again implemented by
a special pattern of register synchronization packets being sent by the client.

Please note that using any of these extra bits of functionality in your quest
will mean that it will not be usable without significant work on other PSO
servers than Sylverant. That is not to say that other servers couldn't implement
the same functionality, but as of now, none have (that I am aware of).

## Server-side Quest Flags

Server-side quest flags give quests the ability to store data in chunks of
16-bits on the server on a per guild card basis. The idea here was for small
pieces of data like the completion of a quest or sub-goal of a quest that can
affect other quests to be stored in this fashion, much like the quest flags that
the game already stores in the save files. However, the server can store many
more flags than can the game itself in its save file (as space is "unlimited"
on the server). Quests on Sylverant can (after getting permission from the
server administration) use these server-side flags and have them stored in the
database on the Shipgate. This section describes one method for accessing the
server-side quest flags (they are also accessible through quest functions).

To use the server-side quest flags, the quest configuration must have a `sflag`
attribute telling which register will be used for communicating with the server
to read/write the flag data. In doing so, any synchronization requests for that
register will be handled by the Ship Server and not forwarded to the rest of the
team as would normally be done. There are three operations that can be performed
on a flag in this manner: setting, getting, and deleting a flag value.

The format for server-side quest flag requests is as follows (each letter is a
nibble -- registers are 32-bits in length):
```
CCFFDDDD

CC = Control Bits
FF = Flag Number
DDDD = Flag Data
```

The Control Bits are defined as follows (each character here is a bit):
```
EAW0D000

E = Error
A = Acknowledge
W = Write
D = Delete
All other bits should always be 0.
```

Any `sync_register`, `sync_let`, or `sync_leti` operations performed by the
quest on the register specified as the `sflag` register in the quest
configuration will be treated as a request to the server for flag data. The
server will respond to the request by sending a new register value back to the
quest with its response. The error and acknowledge bits above are only to be set
by the server in responses and must never be set on a request sent to the
server. Setting either of these bits on a request to the server will result in
the server returning an error and ignoring the request. In order to read a flag
from the server, a request must be sent to the server with no flag bits set, the
and the flag number filled in (the data bits are ignored on reads). To set a
flag, a request must be sent to the server with the W bit set in the control
bits (and no other bits set), the flag number filled in, and the data bits
filled in with the desired value. To delete a flag's value, a request must be
sent to the server with the D bit set in the control bits (and no other bits
set) and the flag number filled in (the data bits are ignored on deletes).

Once the server receives a request, it will act on the request and respond
accordingly. If the operation succeeds, the response will have the acknowledge
bit set, the appropriate operation bit set, and the flag number filled in (reads
will also have the data bits filled in on return). If an error occurs, the error
bit will be set and the low-order 16 bits of the register value will indicate
the error that occurred. Currently the following errors are defined:

* `0x0001` - Read requested for flag that hasn't been set.
* `0xFFFE` - Invalid control bit sequence on request.
* `0xFFFF` - Undefined server error processing request.

As the flag operations do involve a round-trip to the server, the results will
not immediately appear in the quest. Your quest code must submit the request and
then wait for a response. You should implement some sort of timeout so that if
something goes wrong that you can retry the request.

As a short example, here's a piece of QEdit-style PSO Quest Assembly that could
be used to read the server flag number 1 (assuming `sflag` is set to 60 and that
comments begin with a # character and wouldn't be in the real quest asm):

```
800:    sync_leti R60, 00010000     # R60 is the sflag register
        leti R61, 00000000          # R61 is a counter
        leti R60, FFFFFFFF          # Initialize R60 to a sentinel value
        sync                        # Wait one frame
801:    jmpi_!= R60, FFFFFFFF, 802  # Did the server respond?
        jmpi_>= R61, 450, 804       # Have we timed out? (15 seconds)
        addi R61, 00000001          # Increment our counter
        sync                        # Wait another frame
        jmp 801                     # Try another loop
802:    jmpi_= R60, 80000001, 803   # Is the flag not yet set?
        ujmpi_>= R60, 80000000, 805 # Is there some other error?
        andi R60, 0000FFFF          # Mask off the control and flag number
        let R80, R60                # Move the result to R80
        ret                         # Return
803:    leti R80, 00000000          # Set R80 to a default value (0)
        ret                         # Return
804:    window_msg 'Request timed out.'
        winend
        leti R80, FFFFFFFF
        ret
805:    window_msg 'An error has occurred.'
        winend
        leti R80, FFFFFFFF
        ret
```

Obviously this code is only meant as a template, not as necessarily the one and
only correct way to do this. There's plenty of places it could be improved (like
actually retrying on timeout, for instance).

Also, it is important to note that this functionality is also available through
the server-side quest functions defined in the next section (specifically,
functions 12, 13, and 16 for reading, writing, and deleting respectively).

## Server-side Quest Functions

The functionality described above for server-side flags is fairly limited in
what it can support. To this end, Sylverant also implements support for a more
general system of server-side quest functions. Originally the thought here was
to replicate some functionality that earlier versions of the game lack in their
quest implementations, but the server could potentially go well beyond that, as
more functions are implemented.

To implement this functionality, Ship Server maintains a small stack that quests
can push to using register synchronization operations. Each client has its own
stack on the server, which can hold a maximum of 32 values at a time. To use
this functionality, the quest must define a control and a data register in the
quest configuration (with the `ctlreg` and `datareg` attributes). If these
registers are defined in the quest configuration, any synchronization operations
performed on these registers will be intercepted by the server and not forwarded
to other clients as usual. To push to the stack, the quest should use the
`sync_let`, `sync_leti`, or `sync_register` opcodes to the data register. The
stack will automatically be cleaned up when a server-side quest function is run.
The only use of the control register at this time is to clear the stack. To do
so, you must sync a zero value to the control register (actually, any value will
currently work, but for future compatibility, please use a zero value).

The stack frame should be built by first pushing the function number to call
(see the list later in this section), then the number of arguments should be
pushed to the stack, followed by the number of return values. After this, the
values for the arguments should be pushed in left-to-right order, followed by
register numbers to store the results of the call in. Once the server acts on
the function call, it will sync all return values (if the call succeeds) and
then will sync the data register with an error code (or 0 if the call
succeeds). The error codes currently defined are as follows:

* `0x00000000` - Success
* `0x8000FFFF` - Stack overflow.
* `0x8000FFFE` - Unknown function number.
* `0x8000FFFD` - Invalid number of arguments for function.
* `0x8000FFFC` - Invalid number of return registers for function.
* `0x8000FFFB` - Invalid argument value.
* `0x8000FFFA` - Invalid return register number.
* `0x8000FFF9` - Stack has been locked (probably due to an outstanding call that
has not yet completed.
* `0x8000FFF8` - Error processing request on Shipgate.

As a short example, the following code demonstrates how to call the
`get_section_id` server-side quest function. This function takes one argument
representing which client to retrieve the section ID of (which should be 0-3).
If the argument is instead -1, it will retrieve all players section IDs (and
thus will need four return registers instead of one). The first segment of code
here demonstrates how to call this function with a single player specified
(assuming the data register is set to 60 again and comments that wouldn't be
in the real quest script are denoted starting with a #):

```
100:    sync_leti R60, 0            # Function 0 = get_section_id
        sync_leti R60, 1            # Passing one argument
        sync_leti R60, 1            # Getting one return value
        sync_leti R60, 0            # Retrieve client ID 0's section
        sync_leti R60, 80           # Into R80
        leti R60, FFFFFFFF          # Set R60 to a sentinel value
        sync                        # Wait one frame
101:    jmpi_= R60, 0, 102          # Has the server responded success?
        jmpi_!= R60, FFFFFFFF, 103  # Has it responded error?
        sync                        # Wait another frame
        jmp 101                     # Try again
102:    ret                         # Return (the section id is in R80)
103:    window_msg 'An error has occurred.'
        winend
        leti R80, FFFFFFFF
        ret
```

This next segment of code shows how the code would differ if we instead were to
want all players' section ID values.

```
100:    sync_leti R60, 0            # Function 0 = get_section_id
        sync_leti R60, 1            # Passing one argument
        sync_leti R60, 4            # Getting four return values
        sync_leti R60, FFFFFFFF     # Retrieve all players' section IDs
        sync_leti R60, 80           # Client 0 into R80
        sync_leti R60, 81           # Client 1 into R81
        sync_leti R60, 82           # Client 2 into R82
        sync_leti R60, 83           # Client 3 into R83
        leti R60, FFFFFFFF          # Set R60 to a sentinel value
        sync                        # Wait one frame
101:    jmpi_= R60, 0, 102          # Has the server responded success?
        jmpi_!= R60, FFFFFFFF, 103  # Has it responded error?
        sync                        # Wait another frame
        jmp 101                     # Try again
102:    ret                         # Return (the section ids are in R80-R83)
103:    window_msg 'An error has occurred.'
        winend
        leti R80, FFFFFFFF
        leti R81, FFFFFFFF
        leti R82, FFFFFFFF
        leti R83, FFFFFFFF
        ret
```

Just as with the server-side flags, you should implement some sort of timeout
with a possible retry for any of these function calls (when a timeout occurs,
you should also write 0 to the control register to clear the stack).

### Server-side Quest Function Definitions

Each of the server-side quest functions are defined in this section, including
the arguments they accept and how many returns they produce. The functions are
only very briefly described, as most of them are fairly self-explanatory.

* Function 0: get_section_id
    * Arguments: 1: int id -- Set to a client ID from 0-3 for a single player
or -1 to retrieve all players.
    * Returns: 1 or 4 values of the requested section IDs.

* Function 1: get_server_time
    * Arguments: None
    * Returns: 1 value representing the current time as the number of seconds
since 1/1/1970 @ 0:00:00 UTC. This may be a signed value depending on the
underlying OS of the server. Please note that this will only potentially become
a problem in 2038.

* Function 2: get_client_count
    * Arguments: None
    * Returns: 1 value representing the number of clients currently in the team.

* Function 3: get_character_class
    * Arguments: 1: int id -- Set to a client id from 0-3 for one player or -1
for all players in the team.
    * Returns: 1 or 4 values of the requested character classes

* Function 4: get_character_gender
    * Arguments: 1: int id -- Set to a client id from 0-3 for one player or -1
for all players in the team.
    * Returns: 1 or 4 values of the requested genders, as defined by the game.

* Function 5: get_character_race
    * Arguments: 1: int id -- Set to a client id from 0-3 for one player or -1
for all players in the team.
    * Returns: 1 or 4 values of the requested character races, as defined by
the game.

* Function 6: get_character_job
    * Arguments: 1: int id -- Set to a client id from 0-3 for one player or -1
for all players in the team.
    * Returns: 1 or 4 values of the requested character jobs, as defined by
the game.

* Function 7: get_client_floor
    * Arguments: 1: int id -- Set to a client id from 0-3 for one player or -1
for all players in the team.
    * Returns: 1 or 4 values of the floor of the requested player(s).

* Function 8: get_position
    * Arguments: 1: int id -- Set to a client id from 0-3 for one player or -1
for all players in the team.
    * Returns: 1 or 4 values of the positions of the requested player(s).
    * Note: Each return value takes up three registers. Only the first of the
three are specified. Also, note that all values are truncated to integers.

* Function 9: get_random_integer
    * Arguments:
        * 1: int min -- The minimum value for the random number.
        * 2: int max -- The maximum value for the random number.
    * Returns: 1 value: The randomly generated 32-bit integer.

* Function 10: get_ship_client_count
    * Arguments: None
    * Returns: 1 value: The number of clients currently on the ship.

* Function 11: get_block_client_count
    * Arguments: None
    * Returns: 1 value: The number of clients currently on the block.

* Function 12: get_short_qflag
    * Arguments: 1: int flag -- The flag number to request from the server.
    * Returns: 1 value: The 16-bit value of the specified flag on the shipgate.
On error, this will be negative.
        * -1: Invalid flag number.
        * -2: Shipgate has disappeared.
        * -3: Flag is currently unset.

* Function 13: set_short_qflag
    * Arguments:
        * 1: int flag -- The flag number to request from the server.
        * 2: uint16_t val -- The value to set in the flag.
    * Returns: 1 value: 0 on success. On error this will be negative.
        * -1: Invalid flag number.
        * -2: Shipgate has disappeared.

* Function 14: get_long_qflag
    * Arguments: 1: int flag -- The flag number to request from the server.
    * 1 value: The 32-bit value of the specified flag on the shipgate. On error,
this will be negative.
        * -1: Invalid flag number.
        * -2: Shipgate has disappeared.
        * -3: Flag is currently unset.

* Function 15: set_long_qflag
    * Arguments:
        * 1: int flag -- The flag number to request from the server.
        * 2: uint32_t val -- The value to set in the flag.
    * Returns: 1 value: 0 on success. On error, this will be negative.
        * -1: Invalid flag number.
        * -2: Shipgate has disappeared.

* Function 16: del_short_qflag
    * Arguments: 1: int flag -- The flag number to delete.
    * Returns: 1 value: 0 on success. On error, this will be negative.
        * -1: Invalid flag number.
        * -2: Shipgate has disappeared.

* Function 17: del_long_qflag
    * Arguments: 1: int flag -- The flag number to delete.
    * Returns: 1 value: 0 on success. On error, this will be negative.
        * -1: Invalid flag number.
        * -2: Shipgate has disappeared.

* Function 18: word_censor_check
    * Arguments: 1...n: char str[] -- The string to check against the censor.
This string may be NUL terminated, but is not required to be. Only ASCII values
are accepted. The maximum length accepted is 24 characters.
    * Returns: 1 value: 0 on nothing matched by the censor, 1 if matched.

* Function 19: word_censor_check2
    * Arguments: 1...n: char str[] -- The string to check against the censor.
This string may be NUL terminated, but is not required to be. Only values 0-26
are accepted (mapping to NUL, then A-Z). The maximum length accepted is 24
characters.
    * Returns: 1 value: 0 on nothing matched by the censor, 1 if matched.

* Function 20: get_team_seed
    * Arguments: None
    * Returns: 1 value: The random seed chosen when the team was created.

* Function 21: get_pos_updates
    * Arguments: 1: int id -- Set to the id to subscribe to updates for or -1 to
get them for all clients in the team.
    * Returns: 1 or 4 values for storing the positions of the client(s).
    * Note: Each return takes up 4 registers (x,y,z,f). Only the first of these
are specified. Additionally, 0 is not a supported value. All values are
truncated to integers. The registers specified will be updated periodically with
new values without any further interaction with this function.

* Function 22: get_level
    * Arguments: 1: int id -- Set to a client id from 0-3 for one player or -1
for all players in the team.
    * Returns: 1 or 4 values of the requested levels.
