import express from "express";
import jsonwebtoken from "jsonwebtoken";
import path from "path";
import http from "http";

const app = express();

const httpPort = 9092;
const secret = "secret";

function createToken(pid: string, sid: string) {
  return jsonwebtoken.sign(
      { 
        iss: 'chat_auth',
        pid: pid,
        sid: sid,
        exp: Math.floor(Date.now() / 1000) + 60 * 60,
        data: { }
      },
      secret
    );
}

app.get("/issue/:room/:user", (req, res) => {
  if(req.params.room.length < 1000 && req.params.user.length < 1000) {
    res.send(createToken(req.params.user, req.params.room));
  }
});

app.use(express.static(path.join(__dirname, "..", "..", "build")));

app.use((req, res, next) => {
  res.sendFile(path.join(__dirname, "..", "..", "build", "index.html"));
});

var httpServer = http.createServer(app);

console.log("server now listening at http://localhost:" + httpPort);

httpServer.listen(httpPort);
