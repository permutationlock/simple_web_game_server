### Minimal Templates

This folder contains minimal templates for setting up game and matchmaking
servers with and without TLS. The **minimal_game.hpp** file defines the classes
**minimal_game** and **minimal_matchmaker** which define a bare bones classes
satisfying the template concepts for game and matchmaker used in the
*Simple Web Game Server* library. Each subdirectory contains corresponding
example code to setup a server using the classes defined in
**minimal_game.hpp**.

For the TLS examples, you must have an
authorized certificate for the domain name where you server the application. To
achieve this for localhost I recommend installing
[mkcert](https://github.com/FiloSottile/mkcert). Then you can generate the
certificate key pair:

```shell
mkcert -key-file key.pem -cert-file cert.pem localhost
```

To run a matchmaking server example:

```shell
cd matchmaking_server
make
./matchmaking_server
```

To run a game server example:

```shell
cd game_server
make
./game_server
```
