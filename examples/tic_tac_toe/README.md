### Tic Tac Toe Example

This folder contains an example Tic Tac Toe web game, useful to see how to set
up a full game system with matchmaking and game reporting. The
game_server folder contains C++ code
for the Tic Tac Toe WebSocket game server, the matchmaking_server folder
contains the corresponding matchmaking server, and the client directory
contains a [React](https://reactjs.org/)
client and [Express.js](https://expressjs.com/) web server to serve the
frontend for the Tic Tac Toe app, issue JWTs for the matchmaker, verify JWTs
for game results, and run a few other features.

Because this app uses TLS for the HTTPS and WSS protocols, you must have an
authorized certificate for the domain name where you serve the application. To
achieve this for localhost I recommend installing
[mkcert](https://github.com/FiloSottile/mkcert). Then you can generate the
certificate key pair:

```shell
mkcert -key-file key.pem -cert-file cert.pem localhost
```

To run the client:

```shell
cd client
npm start
```

To run the matchmaking server:

```shell
cd server
make
./matchmaking_server
```

To run the game server:

```shell
cd game_server
make
./game_server
```

Once all three are running, go to
[https://localhost:9092](https://localhost:9092) in any
browser to access the app.
