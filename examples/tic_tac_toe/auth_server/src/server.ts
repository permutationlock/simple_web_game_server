import express from "express";
import jsonwebtoken from "jsonwebtoken";

const app = express();
const port = 9092;

const secret = "secret";

var id_count = 0;
var sid_count = 0;

function create_token(new_id: number) {
  return jsonwebtoken.sign(
      { iss: 'tic_tac_toe_auth', pid: new_id, sid: new_id, data: {} },
      secret
    );
}

app.get("/submit", (req, res) => {
  res.send(verify_game_token);
});

app.get("/match", (req, res) => {
  res.send(create_match_token(id_count++));
});

app.get("/login", (req, res) => {
  res.send(create_id_token(id_count++));
});


app.listen(port, () => {
  console.log(`Authentication server listening at http://localhost:${port}`);
});
