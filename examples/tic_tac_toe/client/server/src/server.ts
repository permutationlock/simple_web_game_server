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

const userDBFilePath = "../data/users.db";
let userDB = new Datastore({ filename: userDBFilePath, autoload: true });

const myUpdateOpts: UpdateOptions = {
  multi: false,
  upsert: true,
  returnUpdatedDocs: false
};

var pid = 0;
serverDB.findOne({ name: "pid" }, (err, doc) => {
  if(doc != null) {
    if("value" in doc) {
      pid = <number>(doc.value);
    }
  } else {
    serverDB.insert({ name: "pid", value: pid });
  }
});



var sid = 0;
serverDB.findOne({ name: "sid" }, (err, doc) => {
  if(doc != null) {
    if("value" in doc) {
      sid = <number>(doc.value);
    }
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
        exp: Math.floor(Date.now() / 1000) + 60,
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

app.get("/login/:token", (req, res) => {
  let loginOpts: VerifyOptions = { issuer: 'tic_tac_toe_login' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      loginOpts,
      (err, decoded: object | undefined) => {
        if(decoded != undefined && (<LoginToken>decoded).pid !== undefined) {
          let id: number = (<LoginToken>decoded).pid;
          serverDB.findOne({ pid: id }, (err, doc) => {
            if(doc != null) {
              if("token" in doc) {
                let playerOpts: VerifyOptions = {
                  issuer: 'tic_tac_toe_auth'
                };
                let token = <string>(doc.token);
                jsonwebtoken.verify(
                    token,
                    secret,
                    playerOpts,
                    (err, decoded: object | undefined) => {
                      if(decoded != undefined) {
                        console.log("sending old match token: " + token);
                        res.send(token);
                      } else {
                        token = createMatchToken(id);
                        console.log("old token expired, sending new match token: " + token);
                        res.send(token);
                        serverDB.update(
                            { pid: id },
                            { pid: id, token: token },
                            myUpdateOpts,
                            () => {}
                          );
                      }
                    }
                  );
              }
            } else {
              let token = createMatchToken(id);
              console.log("creating match token: " + token);
              res.send(token);
              serverDB.insert({ pid: id, token: token });
            }
          });
        }
      }
    );
});

app.get("/submit/:token", (req, res) => {
  let opts: VerifyOptions = { issuer: 'tic_tac_toe_game_server' };
  jsonwebtoken.verify(
      req.params.token,
      secret,
      opts,
      (err, decoded: object | undefined) => {
        if(decoded != undefined) {
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

httpsServer.listen(httpsPort);
