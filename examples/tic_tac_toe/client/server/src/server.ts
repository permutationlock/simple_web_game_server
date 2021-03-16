import express from "express";
import jsonwebtoken from "jsonwebtoken";
import { VerifyOptions } from "jsonwebtoken";
import path from "path";
import http from "http";
import https from "https";
import fs from "fs";
import Datastore from "nedb";
import { UpdateOptions } from "nedb";

const key = fs.readFileSync("../../key.pem", "utf8");
const cert = fs.readFileSync("../../cert.pem", "utf8");
const app = express();

const httpsPort = 9092;
const credentials = { key: key, cert: cert };

const secret = "secret";

const serverDBFilePath = "../data/server.db";
let serverDB = new Datastore({ filename: serverDBFilePath, autoload: true });

interface ServerDBItem {
  name: string,
  value: number
};

function isServerDBToken(token : any): token is LoginToken {
  return ('pid' in  token);
}

const userDBFilePath = "../data/users.db";
let userDB = new Datastore({ filename: userDBFilePath, autoload: true });

interface UserDBItem {
  pid: number,
  token: string
};

const myUpdateOpts: UpdateOptions = {
  multi: false,
  upsert: true,
  returnUpdatedDocs: false
};

var pid = 0;
serverDB.findOne({ name: "pid" }, (err, doc) => {
  if(doc != null && isServerDBItem(doc)) {
    let item = <ServerDBItem>doc;
    pid = item.value;
  } else {
    serverDB.insert({ name: "pid", value: pid });
  }
});


var sid = 0;
serverDB.findOne({ name: "sid" }, (err, doc) => {
  if(doc != null && isServerDBItem(doc)) {
    let item = <ServerDBItem>doc;
    sid = item.value;
  } else {
    serverDB.insert({ name: "sid", value: sid });
  }
});

function createPlayerToken() {
  pid += 1;
  serverDB.update({ name: "pid" }, { $set: { name: "pid", value: pid } },
                myUpdateOpts, () => {});
  return jsonwebtoken.sign(
      { iss: 'tic_tac_toe_login', pid: pid, data: {} },
      secret
    );
}

function createMatchToken(id: number) {
  sid += 1;
  serverDB.update({ name: "sid" }, { $set: { name: "sid", value: sid } },
                  myUpdateOpts, () => {});
  return jsonwebtoken.sign(
      { 
        iss: 'tic_tac_toe_auth',
        pid: id,
        sid: sid,
        exp: Math.floor(Date.now() / 1000) + 1000,
        data: {}
      },
      secret
    );
}

app.get("/signup", (req, res) => {
  res.send(createPlayerToken());
});

interface LoginToken {
  pid: number
};

function isLoginToken(token : object): token is LoginToken {
  return ('pid' in  token);
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
          userDB.findOne({ pid: claims.pid }, (err, doc) => {
            if(doc != null && isUserDBItem(doc)) {
              let item = <UserDBItem>doc;
              let playerOpts: VerifyOptions = {
                issuer: 'tic_tac_toe_auth'
              };
              let token = item.token;
              jsonwebtoken.verify(
                  token, secret, playerOpts,
                  (err, decoded: object | undefined) => {
                    if(decoded != undefined) {
                      console.log("sending old match token: " + token);
                      res.send(token);
                    } else {
                      token = createMatchToken(id);
                      console.log("old token expired, sending new match token: " + token);
                      res.send(token);
                      userDB.update(
                          { pid: id },
                          { pid: id, token: token },
                          myUpdateOpts,
                          () => {}
                        );
                    }
                  }
                );
            } else {
              let token = createMatchToken(id);
              console.log("creating match token: " + token);
              res.send(token);
              userDB.insert({ pid: id, token: token });
            }
          });
        }
      }
    );
});

app.get("/cancel/:token", (req, res) => {
  let opts: VerifyOptions = { issuer: 'tic_tac_toe_matchmaker' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      opts,
      (err, decoded: object | undefined) => {
        if(decoded != undefined && isLoginToken(decoded)) { 
          let claims = <LoginToken>decoded;
          userDB.update(
              { pid: claims.pid },
              { pid: claims.pid, token: "" },
              myUpdateOpts,
              () => {}
            );
          res.send({ success: true });
        } else {
          res.send({ success: false });
        }
      }
    );
});

app.get("/submit/:token", (req, res) => {
  let submitOptions: VerifyOptions = { issuer: 'tic_tac_toe_game_server' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      submitOptions,
      (err, decoded: object | undefined) => {
        if(decoded != undefined && isLoginToken(decoded)) { 
          let claims = <LoginToken>decoded;
          userDB.update(
              { pid: claims.pid },
              { pid: claims.pid, token: "" },
              myUpdateOpts,
              () => {}
            );
          res.send({ success: true });
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

var httpsServer = https.createServer(credentials, app);

console.log("listing at https://localhost:" + httpsPort);

httpsServer.listen(httpsPort);
