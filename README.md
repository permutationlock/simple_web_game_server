### Simple Web Game Server

This project provides generic C++ classes to create multi-threaded game servers
and matchmaking servers for browser games using WebSockets and JWT
authentication. It is built
upon the [WebSocket++](https://github.com/zaphoyd/websocketpp) and
[JWT++](https://github.com/Thalhammer/jwt-cpp) libraries.

The source code is available on
[GitHub](https://github.com/permutationlock/simple_web_game_server).
[Doxygen](https://www.doxygen.nl/index.html) documentation is available
[here](https://permutationlock.com/simple_web_game_server/).

#### Dependencies

You must have a C++14 compatible compiler as well as have the
[ASIO](https://think-async.com/Asio/) library installed in some form.
Parallelization of game updates uses the C++17 <execution> header, so a C++17
compatible compiler is required for that feature.

#### Motivation

The core motivation for the libarary is to:

 - provide a C++ framework for creating servers to run online multiplayer
   browser games and to perform algorithmic matchmaking;
 - be flexible and simple by providing as little as possible
   beyond the core functions;
 - be secure and authenticate clients in order to support competitive games;
 - have servers run independently with no external communication
   beyond messages to clients;
 - be performant and allow both horizontal and vertical scaling.

In order to create a backend for a game,
all that needs to be written is a specification for the JWTs that you want to
verify, a class describing the game logic, and a class detailing the
matchmaking algorithm; see examples below.

Security is achieved by via TLS and the WSS protocol. Clients must also
initiate each connection to the server with a JWT
containing their identity and detailing
their intended session with the server.

With regards point four and server isolation, the servers in this
library are designed to only listen and communicate to clients, and to
make no other external communication, such as updating a databases.
Each time a server session is completed, the server will send each
associated client a token verifying the result. Thus, if it is desired that server
activity be tracked, for example to track ranked matches in a
competitive game, each client may be designed to submit their result tokens to a
central location for tracking. In order to guard against
malicious clients there must be significant incentive for clients to
submit their results, but this will usually exist
naturally, e.g. the winner in a competitive game will always wish to
submit the result.

Since servers run completely independently, it is easy to achieve horizontal
scaling by simply spinning up as many servers as desired and then pointing new
clients to them with the appropriate JWT authentication.

Vertical scaling is accommodated by allowing for multi-threading in several
aspects of the server: multiple threads may be assigned to handle WebSocket
connections and server
actions, and the game update loop may update games in
parallel.
In general the most benefit from parallelization
should come from processing game updates.

#### Examples

 - [Minimal Template](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/minimal_template):
   minimal examples of game and matchmaking servers, with and without TLS.
 - [Chat](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/chat):
   a simple chat server displaying the basic features of the game server
   and the player/session id system.
 - [Tic Tac Toe](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/tic_tac_toe):
   a full implementation of a competitive tic tac toe game with elo ranking,
   matchmaking, and allowing for clients to reconnect to games.

#### Games using this library

 - [Krynth.io](https://krynth.io): a one-one-one competitive deduction game.

#### Basic functionality

The base server class wraps a
[WebSocket++](https://github.com/zaphoyd/websocketpp) server and provides JWT
authentication as well as player and session identification for clients.
A player id is an identifier unique to each player while a session id is unique
to a particular session of interaction with the server.

When the first client with a given session id connects and provides a verified
JWT, a session is started. Each
session persists until it is ended by the server, at which point a result
token string is sent back to all participating clients.
Completed sessions are archived for a period of
time to allow clients to retrieve the result token; this allows
disconnected players to reconnect and see the result of a game for example.

In a game server a session represents a particular
game, with different players sharing the same session id if they are in the
same game. In a matchmaking server sessions have multiple potential uses
depending on the specification, e.g. allowing multiple players to queue for
a match together by sharing a session.
