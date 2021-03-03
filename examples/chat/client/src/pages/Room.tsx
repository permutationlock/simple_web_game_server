import React from 'react';
import './Room.css';
import { RouteComponentProps } from 'react-router-dom';

var closeReasons = new Set([
    "INVALID_TOKEN",
    "DUPLICATE_CONNECTION",
    "SERVER_SHUTDOWN",
    "SESSION_COMPLETE"
  ]);

type RoomState = {
  socket: WebSocket | null,
  token: string | null,
  text: string,
  message: string
};

type RoomParams = { name: string, session: string };
type RoomProps = RouteComponentProps<RoomParams>;

class Room extends React.Component<RoomProps, RoomState> {
  textLog: React.RefObject<HTMLTextAreaElement>;

  constructor(props : RoomProps) {
    super(props);

    this.state = {
      socket: null,
      token: null,
      text: "",
      message: ""
    };

    this.textLog = React.createRef();
  }

  componentDidMount() {
    const { name, session } = this.props.match.params;
    
    const signupUri = "http://localhost:9092/issue/" + session + "/" + name;
    if(this.state.token == null) {
      fetch(signupUri)
        .then(response => response.text())
        .then((tokenStr) => {
          console.log('token: ' + tokenStr);
          this.setState({ token: tokenStr });
          this.connect();
        });
    }
  }

  componentDidUpdate() {
    if(this.textLog !== null) {
      if(this.textLog.current !== null) {
        this.textLog.current.scrollTop = this.textLog.current.scrollHeight;
      }
    }
  }

  connect() {
    if(this.state.socket === null) {
      // change to ws if not using tls
      var ws_uri = "ws://localhost:9090";
      var ws = new WebSocket(ws_uri);

      console.log("attempting to connect to " + ws_uri);

      ws.onopen = () => {
        if(ws != null && this.state.token != null) {
          ws.send(this.state.token);
          console.log("connected to " + ws_uri);
          this.setState({
              socket: ws
            });
        }
      };

      ws.onmessage = (e) => {
        console.log("received ws message: " + e.data);
        this.setState({
            text: this.state.text + e.data + "\n"
          });
      };

      ws.onclose = (e) => {
        console.log("disconnected from " + ws_uri +": " + e.reason);
        this.setState({ socket: null });

        if(closeReasons.has(e.reason)) {
          // disconnect for valid reason, session over
          console.log("closed for reason: " + e.reason);
        } else {
          // disconnected for unknown reason, attempt to reconnect
          setTimeout(this.connect.bind(this), 1000);
          console.log("closed for unknown reason");
        }
      };
    }
  }

  onMessageChange(e: React.ChangeEvent<HTMLInputElement>) {
    this.setState({ message: e.target.value });
  }

  sendMessage() {
    if(this.state.socket != null) {
      this.state.socket.send(this.state.message);
      this.setState({ message: "" });
    }
  }

  keyPress(e: React.KeyboardEvent<HTMLInputElement>) {
    console.log("key press: " + e.keyCode);
    if(e.key === 'Enter') {
      this.sendMessage();
    }
  }

  render() {
    return (
      <div className="Room">
        <textarea id="Room-chat" ref={this.textLog} rows={8} cols={50}
          value={this.state.text} readOnly={true}
        /><br/>
        <input type="text" id="Room-message" value={this.state.message}
          onChange={this.onMessageChange.bind(this)}
          onKeyPress={this.keyPress.bind(this)}
          autoFocus={true}
        />
        <button disabled={this.state.socket === null}
          onClick={this.sendMessage.bind(this)}>Send</button>
      </div>
    );
  }
};

export default Room;
