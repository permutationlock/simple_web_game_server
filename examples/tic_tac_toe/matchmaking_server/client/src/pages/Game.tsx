import React from 'react';
import './Game.css';
import { TicTacToe, GameData, parseUpdate, updateGame }
  from '../components/TicTacToe';
import { RouteComponentProps } from 'react-router-dom';

const TIMESTEP = 10;

type GameHomeButtonProps = {
  disabled: boolean,
  onClick: () => void
};

function GameHomeButton(props: GameHomeButtonProps,) {
  return (
    <button onClick={props.onClick} disabled={props.disabled}>
    Back to home
    </button>
  );
}

var closeReasons = new Set([
    "INVALID_TOKEN",
    "DUPLICATE_CONNECTION",
    "SERVER_SHUTDOWN",
    "SESSION_COMPLETE"
  ]);

type GameState = {
  socket: WebSocket | null,
  done: boolean,
  updateInterval: NodeJS.Timer | null,
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
      updateInterval: null,
      gameData: new GameData(),
    };

    this.connect = this.connect.bind(this);
    this.move = this.move.bind(this);
    this.finish = this.finish.bind(this);
    this.goHome = this.goHome.bind(this);
  }

  componentDidMount() {
    this.connect();
  }

  connect() {
    if(this.state.socket === null && this.state.done === false) {
      const { token } = this.props.match.params;

      // change to ws if not using tls
      var ws_uri = "wss://localhost:9090";
      var ws = new WebSocket(ws_uri);

      console.log("attempting to connect to " + ws_uri);

      console.log(token);
      ws.onopen = () => {
        if(ws != null) {
          ws.send(token);
          console.log("connected to " + ws_uri);
          this.setState({
              socket: ws,
              done: false
            });

          if(this.state.updateInterval === null) {
            let updateTimer = () => {
              if(this.state.done) {
                this.finish();
                if(this.state.updateInterval !== null) {
                  clearInterval(this.state.updateInterval);
                  this.setState({ updateInterval: null });
                }
              } else {
                this.setState({
                    gameData: updateGame(this.state.gameData)
                  });
              }
            };

            this.setState({
                updateInterval:
                  setInterval(updateTimer, TIMESTEP)
              });
          }
        }
      };

      ws.onmessage = (e) => {
        console.log("received ws message: " + e.data);
        this.setState({
            gameData: parseUpdate(this.state.gameData, e.data),
          });
      };

      ws.onclose = (e) => {
        console.log("disconnected from " + ws_uri +": " + e.reason);
        this.setState({ socket: null });

        if(closeReasons.has(e.reason)) { 
          // disconnect for valid reason, session over
          console.log("closed for reason: " + e.reason);
          this.setState({
              done: true
            });
        } else {
          // disconnected for unknown reason, attempt to reconnect
          setTimeout(this.connect, 1000);
          console.log("closed for unknown reason");
        }
      };
    }
  }

  finish(): void {
    if(this.state.gameData.token != null) {
      const submitUri = "https://localhost:9091/submit/"
        + this.state.gameData.token;
      fetch(submitUri)
        .then(response => response.text())
        .then((resultStr) => {
          console.log('submit response: ' + resultStr);
        });
    }
  }

  move(newGameData: GameData, updateText : string): void {
    if(this.state.socket != null) {
      this.setState({ gameData: newGameData });
      this.state.socket.send(updateText);
    }
  }

  goHome() {
    this.props.history.push("/");
  }

  render() {
    return (
      <div className="Game">
        <TicTacToe
          gameData={this.state.gameData}
          move={this.move}
          connected={this.state.socket != null}
        />
        <div className="Game-home-button">
          <GameHomeButton onClick={this.goHome} disabled={!this.state.done} />
        </div>
      </div>
    );
  }
};

export default Game;
