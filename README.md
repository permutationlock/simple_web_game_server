### Simple Web Game Server

This project provides generic C++ classes to create multi-threaded game servers
and matchmaking servers for browser games using WebSockets and JWT
authentication. It is built
upon the [WebSocket++](https://github.com/zaphoyd/websocketpp) and
[JWT++](https://github.com/Thalhammer/jwt-cpp) libraries.

In order to create a backend for your game all you need to write is a
specification for the JWTs you want to verify, a
class describing your game logic, and a class detailing your matchmaking
algorithm; see examples below.

#### Examples

 - [Chat](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/chat/client):
   a simple chat server displaying the basic features of the game server.
 - [Tic Tac Toe](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/chat/server):
   a full implementation of a tic tac toe game with matchmaking,
   reconnection to games, and player submission of game and matchmaking results
   to a central server.

#### Main Ideas

The base server class wraps a
[WebSocket++](https://github.com/zaphoyd/websocketpp) server and provides JWT
authentication as well as player and session identification for clients.
A player id is an identifier unique to each player while session id is unique
to a particular session of interaction with the server.

Once the first client with a given session id connects and provides a verified
JWT, the session is started. Each
session persists until it is ended by the server, at which point a result
token string is sent back to all participating clients.
Sessions that have ended are archived for a period of
time to allow clients with valid JWTs to retrieve the result token; this allows
disconnected players to reconnect and see the result of a game for example.

In a game server a session represents a particular
game, with different players sharing the same session id if they are in the
same game. In a matchmaking server sessions have multiple potential uses
depending on the specification, e.g. allowing multiple players to queue for
a match together by sharing a session.
