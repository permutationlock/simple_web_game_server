import express from "express";
import jsonwebtoken from "jsonwebtoken";
import { VerifyOptions } from "jsonwebtoken";
import path from "path";
import https from "https";
import fs from "fs";
import Datastore = require("nedb");
import { UpdateOptions } from "nedb";
import { computeRatingChange } from "./elo";

const key = fs.readFileSync("../../key.pem", "utf8");
const cert = fs.readFileSync("../../cert.pem", "utf8");
const app = express();

const httpsPort = 9092;
const credentials = { key: key, cert: cert };

const secret = "secret";

const myUpdateOpts: UpdateOptions = {
  multi: false,
  upsert: false,
  returnUpdatedDocs: false
};


// Configure Databases

interface ServerDBItem {
  name: string,
  value: number
};

const serverDBFilePath = "../data/server.db";
let serverDB = new Datastore<ServerDBItem>(
  { filename: serverDBFilePath, autoload: true }
);
serverDB.ensureIndex({ fieldName: 'name' });

interface UserDBItem {
  pid: number,
  token: string,
  rating: number
};

const userDBFilePath = "../data/users.db";
let userDB = new Datastore<UserDBItem>(
  { filename: userDBFilePath, autoload: true }
);
userDB.ensureIndex({ fieldName: 'pid' });

interface GameDBItem {
  sid: number,
  players: [ number, number ],
  scores: [ number, number ]
};

const gameDBFilePath = "../data/games.db";
let gameDB = new Datastore<GameDBItem>(
  { filename: gameDBFilePath, autoload: true }
);
gameDB.ensureIndex({ fieldName: 'sid' });


// Load player and session id seeds

var pid = 0;
serverDB.findOne({ name: "pid" }, (err, doc: ServerDBItem | null) => {
  if(doc != null) {
    pid = doc.value;
  } else {
    serverDB.insert({ name: "pid", value: pid });
  }
});

var sid = 0;
serverDB.findOne({ name: "sid" }, (err, doc: ServerDBItem | null) => {
  if(doc != null) {
    sid = doc.value;
  } else {
    serverDB.insert({ name: "sid", value: sid });
  }
});


// Token signing functions

function createPlayerToken() {
  pid += 1;
  serverDB.update({ name: "pid" }, { $set: { value: pid } },
                  myUpdateOpts, () => {});
  userDB.insert({ pid: pid, token: "", rating: 1500 });
  return jsonwebtoken.sign(
      { iss: 'tic_tac_toe_login', pid: pid, data: {} },
      secret
    );
}

function createMatchToken(id: number, rating: number) {
  sid += 1;
  serverDB.update({ name: "sid" }, { $set: { value: sid } },
                  myUpdateOpts, () => {});
  return jsonwebtoken.sign(
      { 
        iss: 'tic_tac_toe_auth',
        pid: id,
        sid: sid,
        exp: Math.floor(Date.now() / 1000) + 18000,
        data: { rating: rating }
      },
      secret
    );
}


// Express app functions

app.get("/signup", (req, res) => {
  res.send(createPlayerToken());
});

interface LoginToken {
  pid: number
};

function isLoginToken(token : any): token is LoginToken {
  return (typeof token.pid === 'number');
}

app.get("/login/:token", (req, res) => {
  let loginOpts: VerifyOptions = { issuer: 'tic_tac_toe_login' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      loginOpts,
      (err, decoded: object | undefined) => {
        if(decoded != undefined && isLoginToken(decoded)) {
          let claims = <LoginToken>decoded;
          userDB.findOne({ pid: claims.pid }, (err, doc: UserDBItem | null) => {
            if(doc != null) {
              let playerOpts: VerifyOptions = {
                issuer: 'tic_tac_toe_auth'
              };
              let token = doc.token;
              jsonwebtoken.verify(
                  token, secret, playerOpts,
                  (err, decoded: object | undefined) => {
                    if(decoded != undefined) {
                      // saved token is still valid, send it
                      res.send(token);
                    } else {
                      // saved token is invalid, generate a new token
                      token = createMatchToken(doc.pid, doc.rating);
                      res.send(token);
                      userDB.update(
                          { pid: claims.pid },
                          { $set: { token: token } },
                          myUpdateOpts,
                          () => {}
                        );
                    }
                  }
                );
            } else {
              console.log("error: match requested for unknown player " +
                          claims.pid);
            }
          });
        }
      }
    );
});

app.get("/info/:token", (req, res) => {
  let loginOpts: VerifyOptions = { issuer: 'tic_tac_toe_login' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      loginOpts,
      (err, decoded: object | undefined) => {
        if(decoded != undefined && isLoginToken(decoded)) {
          let claims = <LoginToken>decoded;
          userDB.findOne({ pid: claims.pid }, (err, doc: UserDBItem | null) => {
            if(doc != null) {
              let playerOpts: VerifyOptions = {
                issuer: 'tic_tac_toe_auth'
              };
              res.send({ success: true, pid: doc.pid, rating: doc.rating });
            } else {
              res.send({ success: false });
            }
          });
        }
      }
    );
});

interface MatchData {
  matched: boolean
};

function isMatchData(data: any): data is MatchData {
  return (typeof data.matched === 'boolean');
}

interface MatchToken {
  pid: number,
  sid: number,
  data: MatchData
};

function isMatchToken(token: any): token is MatchToken {
  return (typeof token.pid === 'number') && (typeof token.sid === 'number')
    && (isMatchData(token.data));
}

app.get("/cancel/:token", (req, res) => {
  let opts: VerifyOptions = { issuer: 'tic_tac_toe_matchmaker' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      opts,
      (err, decoded: object | undefined) => {
        if(decoded != undefined && isMatchToken(decoded)) { 
          let claims = <MatchToken>decoded;
          if(claims.data.matched === false) {
            userDB.update(
                { pid: claims.pid },
                { $set: { token: "" } },
                myUpdateOpts,
                () => {}
              );
            res.send({ success: true });
          }
        } else {
          res.send({ success: false });
        }
      }
    );
});

interface GameData {
  players: [ number, number ],
  scores: [ number, number ]
};

function isGameData(data: any): data is GameData {
  return (typeof data.players[0] === 'number')
    && (typeof data.players[1] === 'number')
    && (typeof data.scores[0] === 'number')
    && (typeof data.scores[1] === 'number');
}

interface GameToken {
  pid: number,
  sid: number,
  data: GameData
};

function isGameToken(token: any): token is GameToken {
  return (typeof token.pid === 'number') && (typeof token.sid === 'number')
    && (isGameData(token.data));
}

function updateRatings(players: [number,number], scores: [number,number]) {
  userDB.find(
    {
      $or: [{ pid: players[0] },
            { pid: players[1] }]
    },
    (err: Error | null, docs: UserDBItem[]) => {
      if(docs.length == 2) {
        let ratings: [ number, number ] = [
            docs[0].rating, docs[1].rating
          ];
        let newRatings = computeRatingChange(
            ratings, scores, 32
          );
        players.forEach((player: number, i: number) => {
          userDB.update(
            { pid: player },
            { $set: { rating: Math.trunc(newRatings[i]) } },
            myUpdateOpts,
            (err, doc) => {}
          );
        });
      }
    }
  );
}

app.get("/submit/:token", (req, res) => {
  let submitOptions: VerifyOptions = { issuer: 'tic_tac_toe_game_server' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      submitOptions,
      (err, decoded: object | undefined) => {
        if(decoded != undefined && isGameToken(decoded)) {
          let claims = <GameToken>decoded;
          userDB.update(
              { pid: claims.pid },
              { $set: { token: "" } },
              myUpdateOpts,
              () => {}
            );
          res.send({ success: true });
          gameDB.findOne({ sid: claims.sid }, (err, doc: GameDBItem | null) => {
            if(doc === null) {
              gameDB.insert({
                sid: claims.sid,
                players: claims.data.players,
                scores: claims.data.scores
              });
              updateRatings(claims.data.players, claims.data.scores);
            }
          });
        } else {
          res.send({ success: false });
        }
      }
    );
});

app.use(express.static(path.join(__dirname, "..", "..", "build")));

app.use((req, res, next) => {
  res.sendFile(path.join(__dirname, "..", "..", "build", "index.html"));
});


// Serve the app via HTTPS

var httpsServer = https.createServer(credentials, app);

console.log("listing at https://localhost:" + httpsPort);

httpsServer.listen(httpsPort);
