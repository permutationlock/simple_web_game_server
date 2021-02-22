import React from 'react';
import './Match.css';
import { RouteComponentProps } from 'react-router-dom';

type MatchState = {
  socket: WebSocket | null,
  matching: boolean,
  matched: boolean,
  searchBar: HTMLElement | null
};

type MatchParams = { token: string };
type MatchProps = RouteComponentProps<MatchParams>;

class Match extends React.Component<MatchProps, MatchState> {
  constructor(props : MatchProps) {
    super(props);

    this.state = {
      socket: null,
      matching: false,
      matched: false,
      searchBar: null
    };
  }

  componentDidMount() {
    const { token } = this.props.match.params;

    var ws = new WebSocket("ws://localhost:9091");

    ws.onopen = () => {
      if(ws != null) {
        ws.send(token);
        this.setState({ socket: ws, matching: true, matched: false });
      }
    };
    
    ws.onmessage = (e) => {
      if(this.state.matching) {
        this.setState({ matching: false, matched: true});
        console.log("game token: " + e.data);
        this.props.history.push("/game/" + e.data);
      }
    };

    ws.onclose = () => {
      if(!this.state.matched) {
        this.setState({ socket: null, matching: false });
        this.props.history.push("/");
      }
    };

    setInterval(() => {
        if(this.state.searchBar === null) {
          
        }
      });
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
        <button disabled={!this.state.matching}
          onClick={this.stopMatchmaking.bind(this)}>Cancel matchmaking</button>

        <p id="Match:search-bar">Searching for game.</p>
      </div>
    );
  }
};

export default Match;
