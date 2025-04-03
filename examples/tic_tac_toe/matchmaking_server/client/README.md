### Tic Tac Toe Client

This directory contains two nested TypeScript applications:

 - the root application is a [React](https://reactjs.org/) game client with an
   implementation of Tic
   Tac Toe; the game client is game independent and simply contains a Tic Tac
   Toe react component, so it might be useful as a starting point for other
   games using this framework;
 - the server directory contains an [Express](https://expressjs.com/) HTTPS
   webserver that serves the client, issues JWTs, and manages a simple
   [Nedb](https://github.com/louischatriot/nedb) database for user accounts.

Executing

```shell
 npm install
 npm start
```

in the root client directory will build the React client and run the webserver.
