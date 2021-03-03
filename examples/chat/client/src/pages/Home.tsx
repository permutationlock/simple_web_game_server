import React from 'react';
import { RouteComponentProps } from 'react-router-dom';
import './Home.css';

type HomeState = { name: string, session: string };
type HomeProps = RouteComponentProps<any>;

class Home extends React.Component<HomeProps, HomeState> {
  constructor(props: HomeProps) {
    super(props);
    this.state = {
      name: "",
      session: ""
    };
  }

  handleNameChange(e: React.ChangeEvent<HTMLInputElement>) {
    this.setState({ name: e.target.value });
  }

  handleRoomChange(e: React.ChangeEvent<HTMLInputElement>) {
    this.setState({ session: e.target.value });
  }

  createRoom() {
    if(this.state.session.length > 0 && this.state.name.length > 0) {
      this.props.history.push("/room/" + this.state.session + "/" +
        this.state.name);
    }
  }

  keyPress(e: React.KeyboardEvent<HTMLInputElement>) {
    console.log("key press: " + e.keyCode);
    if(e.key === 'Enter') {
      this.createRoom();
    }
  }

  render() {
    return (
      <div className='Home'>
        <label>Name:</label>
        <input type="text" id="Home-name"
          value={this.state.name}
          onChange={this.handleNameChange.bind(this)}
          onKeyPress={this.keyPress.bind(this)}
        /><br/>
        <label>Room:</label>
        <input type="text" id="Home-session"
          value={this.state.session}
          onChange={this.handleRoomChange.bind(this)}
          onKeyPress={this.keyPress.bind(this)}
        /><br/>
        <button
          onClick={this.createRoom.bind(this)} id="Home-create-button"
        >
          Start Chatting
        </button>
      </div>
    );
  }
};

export default Home;
