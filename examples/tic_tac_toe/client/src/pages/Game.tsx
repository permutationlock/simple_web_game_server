import React from 'react';
import './Game.css';

type SquareProps = { value: number, onClick: (MouseEvent) => void };

function Square(props: SquareProps) {
  let charFromValue = function(v: number) : string {
    if(v > 0) { return "X"; }
    else if(v < 0) { return "O"; }
    else { return " " }
  };

  return (
    <button className="Game-square" onClick={props.onClick}>
      {charFromValue(props.value)}
    </button>
  );
}

type TimerProps = { name: string, time: number };

function Timer(props: TimeProps) {
  return (
    <div className="Game-timer">
      {this.props.name}: {this.props.time}
    </div>
  );
}

type GameState = {
  socket: WebSocket | null,
  done: boolean,
  board: Array<number>,
  time: number,
  opponent_time: number,
  status: number
};

type GameParams = { token: string };
type GameProps = RouteComponentProps<GameParams>;

class Game extends React.Component<GameProps, GameState> {
  constructor(props : GameProps) {
    super(props);

    this.state = {
      socket: null,
      connected: false,
      done: false,
      board: [ 0, 0, 0,
               0, 0, 0,
               0, 0, 0 ],
      time: 0.00,
      opponent_time: 0.00,
      status: 0
    };

    this.close_reasons = {
      "INVALID_TOKEN",
      "DUPLICATE_CONNECTION",
      "SERVER_SHUTDOWN",
      "SESSION_COMPLETE"
    };
  }

  componentDidMount() {
    connect();
  }

  connect() {
    if(this.state.connected == false && this.state.done == false) {
      const { token } = this.props.match.params;

      var ws_uri = "ws://localhost:9090";
      var ws = new WebSocket(ws_uri);

      console.log("attempting to connect to " + ws_uri);

      console.log(token);
      ws.onopen = () => {
        console.log("connected to " + ws_uri);
        this.setState({
            socket: ws,
            connected: true,
            done: false
          });
        this.state.socket!.send(token);
      };

      ws.onmessage = (e) => {
        console.log("received ws message: " + e.data);
        this.setState({ matching: false, matched: true});
        this.props.history.push("/game/" + e.data);
      };

      ws.onclose = (e) => {
        console.log("disconnected from " + ws_uri +": " + e.reason);
        this.setState({ socket: null, connected: false });

        if(e.reason in this.close_reasons) {
          // disconnect for valid reason, session over
          this.setState({
              done: true
            });
        } else {
          // disconnected for unknown reason, attempt to reconnect
          this.connect();
        }
      };

      // attempt to reconnect in 1 second to verify connection
      setTimeout(this.connect, 1000);
    }
  }

  stopMatchmaking() {
    if(this.state.connected == true) {
      this.state.socket.send("stop");
    }
  }

  render() {
    let renderSquare = function(i: number) {
      return (
        <Square
          value={this.state.board[i]}
          onClick={() => this.props.onClick(i)}
        />
      );
    };

    return (
      <div className="Game">
        <div className="Game-message">
          {this.state.message}
        </div>
        <div className="Game-board-row">
          {this.renderSquare(0)}
          {this.renderSquare(1)}
          {this.renderSquare(2)}
        </div>
        <div className="Game-board-row">
          {this.renderSquare(3)}
          {this.renderSquare(4)}
          {this.renderSquare(5)}
        </div>
        <div className="Game-board-row">
          {this.renderSquare(6)}
          {this.renderSquare(7)}
          {this.renderSquare(8)}
        </div>
        <div className="Game-timers">
          <Timer name="You" time={this.state.time} />
          <Timer name="Opponent" time={this.state.opponent_time} />
        </div>
      </div>
    );
  }
};

export default Game;
