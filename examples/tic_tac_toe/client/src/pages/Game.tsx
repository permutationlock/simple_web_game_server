import React from 'react';
import './Game.css';

type GameState = {
  socket: WebSocket | null,
  playing: boolean,
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
      playing: false,
      done: false,
      board: [ 0, 0, 0,
               0, 0, 0,
               0, 0, 0 ],
      time: 0.00,
      opponent_time: 0.00,
      status: 0
    };
  }

  componentDidMount() {
    const { token } = this.props.match.params;

    var ws = new WebSocket("ws://localhost:9091");

    console.log(token);
    ws.onopen = () => {
      this.setState({ socket: ws, matching: true, matched: false });
      this.state.socket!.send(token);
    };
    
    ws.onmessage = (e) => {
      this.setState({ matching: false, matched: true});
      this.props.history.push("/game/" + e.data);
    };

    ws.onclose = () => {
      if(!this.state.matched) {
        this.setState({ matching: false });
        this.props.history.push("/");
      }
    };
  }

  stopMatchmaking() {
    if(this.state.socket != null) {
      this.state.socket.send("stop");
    }
  }

  render() {
    return (
      <div className="Game">
        <p>searching for game</p>
        <button disabled={!this.state.matching}
          onClick={this.stopMatchmaking.bind(this)}>Cancel matchmaking</button>
      </div>
    );
  }
};

export default Game;
