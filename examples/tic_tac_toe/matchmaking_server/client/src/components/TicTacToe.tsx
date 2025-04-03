import React from 'react';
import './TicTacToe.css';

class GameData {
  board: Array<number>;
  times: Array<number>;
  state: number;
  turn: number;
  player: number;
  done: boolean;
  token: string | null;

  constructor() {
    this.board = [ 0, 0, 0,
                   0, 0, 0,
                   0, 0, 0 ];
    this.times = [ 0.0, 0.0 ];
    this.turn = 0;
    this.state = 0;
    this.player = 0;
    this.done = false;
    this.token = null;
  }
}

function parseUpdate(gameData: GameData, text: string): GameData {
  let updateData = JSON.parse(text);

  let newGameData = gameData;

  let key: keyof GameData;
  for(key in newGameData) {
    if(key in updateData) {
      (newGameData[key] as any) = updateData[key];
    }
  }

  return newGameData;
}

function updateGame(gameData: GameData): GameData {
  let newGameData = gameData;
  newGameData.times[newGameData.turn] -= 10;
  if(newGameData.times[newGameData.turn] < 0) {
    newGameData.times[newGameData.turn] = 0;
  }
  return newGameData;
}

type SquareProps = {
  value: number,
  disabled: boolean,
  onClick: () => void
};

function Square(props: SquareProps) {
  let charFromValue = function(v: number) : string {
    if(v > 0) { return "X"; }
    else if(v < 0) { return "O"; }
    else { return " " }
  };

  return (
    <button
      className="TicTacToe-square"
      onClick={props.onClick}
      disabled={props.disabled}
    >
      {charFromValue(props.value)}
    </button>
  );
}

type StatusBarProps = { yourTurn: boolean, done: boolean,
  state: number, isX: boolean };

function StatusBar(props: StatusBarProps) {
  let message="";

  if(props.done) {
    if(props.state > 0) {
      message = "X's win!";
    } else  if(props.state < 0) {
      message = "O's win!";
    } else {
      message = "It's a draw...";
    }
  } else {
    if(props.yourTurn) {
      message = "Your turn.";
    } else {
      message = "Opponent's turn.";
    }
  }

  return (
    <div className="TicTacToe-status-bar">
      {message}
    </div>
  );
}

type TimerProps = { name: string, time: number };

function Timer(props: TimerProps) {
  return (
    <div className="TicTacToe-timer">
      {props.name}: {(Math.floor(props.time/100)/10.0).toFixed(1)}
    </div>
  );
}

type TicTacToeState = {};

type TicTacToeProps = { 
  gameData : GameData,
  move : (g: GameData, s: string) => void,
  connected: boolean
};

class TicTacToe extends React.Component<TicTacToeProps, TicTacToeState> {
  onClick(square : number) {
    if(this.props.gameData.board[square] === 0 &&
      this.props.gameData.turn === this.props.gameData.player)
    {
      let isX = (this.props.gameData.turn === 0);
      let newGameData = this.props.gameData;
      newGameData.board[square] = isX ? 1 : -1;
      newGameData.turn = (newGameData.turn + 1) % 2;
      this.props.move(
          newGameData,
          JSON.stringify({ "move": [ square % 3, Math.floor(square / 3) ] })
        );
    }
  }

  render() {
    let isX = (this.props.gameData.turn === 0);
    let yourTurn = (this.props.gameData.turn === this.props.gameData.player);
    let player = this.props.gameData.player;
    let oppPlayer = (this.props.gameData.player + 1) % 2;

    let isDeactivated = !this.props.connected
      || this.props.gameData.done
      || !yourTurn;

    let renderSquare = (i: number) => {
      return (
        <Square
          value={this.props.gameData.board[i]}
          disabled={isDeactivated}
          onClick={() => this.onClick(i)}
        />
      );
    };

    let renderRow = (row: number, columns: number) => {
      let data: Array<React.ReactNode> = [];
      for(let i = 0; i < columns; ++i) {
        data.push(renderSquare(row * columns + i));
      }

      return data;
    };

    let renderBoard = (rows: number, columns: number) => {
      let data: Array<React.ReactNode> = [];
      data.push();

      for(let i = 0; i < columns; ++i) {
        data.push(
          <div className="TicTacToe-board-row">
            {renderRow(i, columns)}
          </div>
        );
      }

      return data;
    };

    return (
      <div className="TicTacToe">
        <div className="TicTacToe-game">
          <StatusBar
            done={this.props.gameData.done}
            yourTurn={yourTurn}
            isX={isX}
            state={this.props.gameData.state}
          />
          {renderBoard(3,3)}
          <div className="TicTacToe-timers">
            <Timer name="You" time={this.props.gameData.times[player]} />
            <Timer name="Opponent"
              time={this.props.gameData.times[oppPlayer]} />
          </div>
        </div>
      </div>
    );
  }
};

export { GameData, TicTacToe, parseUpdate, updateGame };
