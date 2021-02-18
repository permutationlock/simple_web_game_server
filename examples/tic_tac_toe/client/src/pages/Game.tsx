import React from 'react';
import './Game.css';
import { TicTacToe, GameData, parseUpdate } from '../components/TicTacToe';
import { RouteComponentProps } from 'react-router-dom';

var closeReasons = new Set([
    "INVALID_TOKEN",
    "DUPLICATE_CONNECTION",
    "SERVER_SHUTDOWN",
    "SESSION_COMPLETE"
  ]);

type GameState = {
  socket: WebSocket | null,
  done: boolean,
  gameData: GameData
};

type GameParams = { token: string };
type GameProps = RouteComponentProps<GameParams>;

class Game extends React.Component<GameProps, GameState> {
  constructor(props : GameProps) {
    super(props);

    this.state = {
      socket: null,
      done: false,
      gameData: new GameData()
    };

    this.connect = this.connect.bind(this);
    this.move = this.move.bind(this);
    this.finish = this.finish.bind(this);
    this.localUpdate = this.localUpdate.bind(this);
  }

  componentDidMount() {
    this.connect();
  }

  connect() {
    if(this.state.socket === null && this.state.done === false) {
      const { token } = this.props.match.params;

      var ws_uri = "ws://localhost:9090";
      var ws = new WebSocket(ws_uri);

      console.log("attempting to connect to " + ws_uri);

      console.log(token);
      ws.onopen = () => {
        console.log("connected to " + ws_uri);
        this.setState({
            socket: ws,
            done: false
          });
        this.state.socket!.send(token);
      };

      ws.onmessage = (e) => {
        console.log("received ws message: " + e.data);
        this.setState({ gameData: parseUpdate(this.state.gameData, e.data) });
      };

      ws.onclose = (e) => {
        console.log("disconnected from " + ws_uri +": " + e.reason);
        this.setState({ socket: null });

        if(closeReasons.has(e.reason)) {
          // disconnect for valid reason, session over
          this.setState({
              done: true
            });
          console.log("closed for reason: " + e.reason);
        } else {
          // disconnected for unknown reason, attempt to reconnect
          setTimeout(this.connect, 1000);
          console.log("closed for unknown reason");
        }
      };
    }
  }

  move(newGameData: GameData, updateText : string): void {
    if(this.state.socket != null) {
      this.setState({ gameData: newGameData });
      this.state.socket.send(updateText);
    }
  }

  localUpdate(newGameData: GameData): void {
    this.setState({ gameData: newGameData });
  }

  finish(): void {
    this.setState({ done: true });
  }

  render() {
    return (
      <div className="Game">
        <TicTacToe
          gameData={this.state.gameData}
          move={this.move}
          finish={this.finish}
          connected={this.state.socket != null}
        />
      </div>
    );
  }
};

export default Game;
