import express from "express";
import jsonwebtoken from "jsonwebtoken";
import path from "path";
import http from "http";
import https from "https";
import fs from "fs";

const key = fs.readFileSync("../../key.pem", "utf8");
const cert = fs.readFileSync("../../cert.pem", "utf8");
const app = express();
const https_port = 9092;
const credentials = { key: key, cert: cert };

const secret = "secret";

var id_count = 0;
var sid_count = 0;

function create_token(new_id: number) {
  return jsonwebtoken.sign(
      { iss: 'tic_tac_toe_auth', pid: new_id, sid: new_id, data: {} },
      secret
    );
}

app.get("/login", (req, res) => {
  res.send(create_token(id_count++));
});

app.use(express.static(path.join(__dirname, "..", "..", "build")));

app.use((req, res, next) => {
  res.sendFile(path.join(__dirname, "..", "..", "build", "index.html"));
});

var httpsServer = https.createServer(credentials, app);

httpsServer.listen(https_port);
