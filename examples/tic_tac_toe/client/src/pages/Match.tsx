import React from 'react';
import './Match.css';
import { RouteComponentProps } from 'react-router-dom';

type MatchState = {
  socket: WebSocket | null,
  matching: boolean,
  matched: boolean
};

type MatchParams = { token: string };
type MatchProps = RouteComponentProps<MatchParams>;

class Match extends React.Component<MatchProps, MatchState> {
  constructor(props : MatchProps) {
    super(props);

    this.state = {
      socket: null,
      matching: false,
      matched: false
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
      if(this.state.matching) {
        this.setState({ matching: false, matched: true});
        this.props.history.push("/game/" + e.data);
      }
    };

    ws.onclose = () => {
      if(!this.state.matched) {
        this.setState({ socket: null, matching: false });
        this.props.history.push("/");
      }
    };
  }

  stopMatchmaking() {
    if(this.state.socket != null) {
      this.setState({ matching: false });
      this.state.socket.send("stop");
    }
  }

  render() {
    return (
      <div className="Match">
        <p>searching for game</p>
        <button disabled={!this.state.matching}
          onClick={this.stopMatchmaking.bind(this)}>Cancel matchmaking</button>
      </div>
    );
  }
};

export default Match;
