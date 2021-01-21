import express from "express";
import jsonwebtoken from "jsonwebtoken";

const app = express();
const port = 9092;

const secret = "secret";

var id_count = 0;

function create_token(new_id: number) {
  return jsonwebtoken.sign(
      { iss: 'tic_tac_toe_auth', id: new_id, data: {} },
      secret
    );
}

app.get("/login", (req, res) => {
  res.send(create_token(id_count++));
});


app.listen(port, () => {
  console.log(`Authentication server listening at http://localhost:${port}`);
});
