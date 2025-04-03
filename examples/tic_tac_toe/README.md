### Tic Tac Toe Example

This folder contains an example Tic Tac Toe web game, useful to see how to set
up a full game system with matchmaking and game reporting. The
game_server folder contains C++ code
for the Tic Tac Toe WebSocket game server. The matchmaking_server folder
contains the corresponding matchmaking server that also serves the web
application. The client sub-directory
contains a [React](https://reactjs.org/)
client Tic Tac Toe app.

Because this app uses TLS for the HTTPS and WSS protocols, you must have an
authorized certificate for the domain name where you serve the application. To
achieve this for localhost I recommend installing
[mkcert](https://github.com/FiloSottile/mkcert). Then you can generate the
certificate key pair:

```shell
 mkcert -key-file key.pem -cert-file cert.pem localhost
```

To build and run the web + matchmaking server:

```shell
 cd matchmaking_server
 make
 ./matchmaking_server
```

To build and run the game server:

```shell
 cd game_server
 make
 ./game_server
```

Once both are running, go to
[https://localhost:9092](https://localhost:9092) in any
browser to access the app.
