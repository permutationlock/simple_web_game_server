### JWT Game Server

This project provides generic C++ classes to create multi-threaded game servers
and matchmaking servers for browser games using WebSockets and JWT
authentication. It is built
upon the [WebSocket++](https://github.com/zaphoyd/websocketpp) and
[JWT++](https://github.com/Thalhammer/jwt-cpp) libraries.

In order to create a backend for your game all you need to write is a
specification of what JWTs you want to verify, a
class describing your game logic, and a class detailing your matchmaking
algorithm. All networking and authentication is handled by the library; see
examples.

The base server class wraps a
[WebSocket++](https://github.com/zaphoyd/websocketpp) server and provides JWT
authentication with player id and session id layers. A player id is an
identifier unique to each player while session id is unique to a particular
session with the server.

Once the first client with a given session id connects and provides a verified
JWT, the session is started. Each
session persists until it is ended by the server, at which point a signed
session result JWT is sent back to all participating clients.
Sessions that have ended are archived for a period of
time; this period should be longer then the expiration time of any associated JWTs.

If a user
attempts to connect with a login JWT for an ended session, they are simply sent the
session result JWT and their connection is terminated. In this way we ensure
that each session takes place at most once, and that each participating client
receives a token to verify their participation in the session.

In a game server a session represents a particular
game, with different players sharing the same session id if they are in the
same game. In a matchmaking server a session simply represents a particular
request for a match. Thus sessions have multiple potential uses in matchmaking,
depending on the specification, e.g. allowing one player to request multiple
matches or allowing multiple players to queue for a match together.
