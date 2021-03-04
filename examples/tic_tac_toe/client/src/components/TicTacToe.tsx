import React from 'react';
import './TicTacToe.css';

class GameData {
  board: Array<number>;
  time: number;
  opponent_time: number;
  xmove: boolean;
  state: number;
  your_turn: boolean;
  done: boolean;
  token: string | null;

  constructor() {
    this.board = [ 0, 0, 0,
                   0, 0, 0,
                   0, 0, 0 ];
    this.time = 0;
    this.opponent_time = 0;
    this.xmove = true;
    this.state = 0;
    this.your_turn = false;
    this.done = false;
    this.token = null;
  }
};

function parseUpdate(gameData: GameData, text: string): GameData {
  let updateData = JSON.parse(text);
  let newGameData = gameData;

  if(updateData["type"] === "game") {
    newGameData["board"] = updateData["board"];
    newGameData["time"] = updateData["time"];
    newGameData["opponent_time"] = updateData["opponent_time"];
    newGameData["xmove"] = updateData["xmove"];
    newGameData["state"] = updateData["state"];
    newGameData["your_turn"] = updateData["your_turn"];
    newGameData["done"] = updateData["done"];
  } else if(updateData["type"] === "time") {
    newGameData["time"] = updateData["time"];
    newGameData["opponent_time"] = updateData["opponent_time"];
  } else if(updateData["type"] === "result") {
    console.log("result token: " + updateData["token"]);
    newGameData["token"] = updateData["token"];
    newGameData["done"] = true;
    newGameData["board"] = updateData["board"];
    newGameData["xmove"] = updateData["xmove"];
    newGameData["state"] = updateData["state"];
  }

  return newGameData;
}

function updateGame(gameData: GameData): GameData {
  let newGameData = gameData;
  if(gameData.your_turn) {
    newGameData.time = newGameData.time - 10;
    if(newGameData.time < 0) {
      newGameData.time = 0;
    }
  } else {
    newGameData.opponent_time = newGameData.opponent_time - 10;
    if(newGameData.opponent_time < 0) {
      newGameData.opponent_time = 0;
    }
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
      this.props.gameData.your_turn)
    {
      let isX = this.props.gameData.your_turn ? this.props.gameData.xmove :
        !this.props.gameData.xmove;
      let newGameData = this.props.gameData;
      newGameData.board[square] = isX ? 1 : -1;
      newGameData.your_turn = false;
      this.props.move(
          newGameData,
          JSON.stringify({ "move": [ square % 3, Math.floor(square / 3) ] })
        );
    }
  }

  render() {
    let isX = this.props.gameData.your_turn ? this.props.gameData.xmove :
      !this.props.gameData.xmove;

    let isDeactivated = !this.props.connected
      || this.props.gameData.done
      || !this.props.gameData.your_turn;

    let renderSquare = (i: number) => {
      return (
        <Square
          value={this.props.gameData.board[i]}
          disabled={isDeactivated}
          onClick={() => this.onClick(i)}
        />
      );
    };

    return (
      <div className="TicTacToe">
        <div className="TicTacToe-game">
          <StatusBar
            done={this.props.gameData.done}
            yourTurn={this.props.gameData.your_turn}
            isX={isX}
            state={this.props.gameData.state}
          />
          <div className="TicTacToe-board-row">
            {renderSquare(0)}
            {renderSquare(1)}
            {renderSquare(2)}
          </div>
          <div className="TicTacToe-board-row">
            {renderSquare(3)}
            {renderSquare(4)}
            {renderSquare(5)}
          </div>
          <div className="TicTacToe-board-row">
            {renderSquare(6)}
            {renderSquare(7)}
            {renderSquare(8)}
          </div>
          <div className="TicTacToe-timers">
            <Timer name="You" time={this.props.gameData.time} />
            <Timer name="Opponent" time={this.props.gameData.opponent_time} />
          </div>
        </div>
      </div>
    );
  }
};

export { TicTacToe, parseUpdate, updateGame, GameData };
