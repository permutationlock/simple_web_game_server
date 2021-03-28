import React from 'react';
import { RouteComponentProps } from 'react-router-dom';
import './Home.css';

type PlayerInfoProps = { pid: number | null, rating: number | null };

function PlayerInfo(props: PlayerInfoProps) {
  if(props.pid === null || props.rating === null) {
    return (
      <div className="Home-player-info">
        Logging In
      </div>
    );
  } else {
    return (
      <div className="Home-player-info">
        PID: {props.pid} Rating: {props.rating}
      </div>
    );
  }
}

type HomeState = {
  token: string | null,
  disabled: boolean,
  pid: number | null,
  rating: number | null
};

type HomeProps = RouteComponentProps<any>;

class Home extends React.Component<HomeProps, HomeState> {
   
  state = {
    token: null,
    disabled: true,
    pid: null,
    rating: null
  };

  componentDidMount() {
    this.login();
  }

  login() {
    const signupUri = "https://localhost:9092/signup";

    const token = sessionStorage.getItem("token");
    if(token === null) {
      fetch(signupUri)
        .then(response => response.text())
        .then((tokenStr) => {
          console.log('player token1: ' + tokenStr);
          sessionStorage.setItem("token", tokenStr);
          this.setState({ token: tokenStr, disabled: false });
          this.getPlayerInfo(tokenStr);
        });
    } else {
      console.log('player token2: ' + token);
      this.setState({ token: token, disabled: false });
      this.getPlayerInfo(token);
    }
  }

  getPlayerInfo(token: string) {
    const infoUri = "https://localhost:9092/info/" + token;
    console.log("fetching: " + infoUri);
    fetch(infoUri)
      .then(response => response.text())
      .then((dataStr) => {
        console.log("fetched: " + dataStr);
        let data: any = JSON.parse(dataStr);
        if(data.success !== undefined && data.success === true) {
          if(data.pid !== undefined && typeof data.pid === 'number') {
            this.setState({ pid: data.pid });
          }
          if(data.rating !== undefined && typeof data.rating === 'number') {
            this.setState({ rating: data.rating});
          }
        }
      });
  }

  handleClick() {
    if(this.state.token !== null) {
      const loginUri = "https://localhost:9092/login/" + this.state.token;

      fetch(loginUri)
        .then(response => response.text())
        .then((tokenStr) => {
          console.log('match request token: ' + tokenStr);
          this.props.history.push("/match/" + tokenStr);
        });
    }
  }

  logout() {
    sessionStorage.removeItem("token");
    this.setState({
      token: null,
      disabled: true,
      pid: null,
      rating: null
    });
    this.login();
  }

  render() {
    return (
      <div className='Home'>
        <PlayerInfo pid={this.state.pid} rating={this.state.rating}/>
        <button disabled={this.state.disabled}
          onClick={this.handleClick.bind(this)}>
          Match
        </button><br/>
        <button disabled={this.state.disabled}
          onClick={this.logout.bind(this)}>
          New Account
        </button>
      </div>
    );
  }
};

export default Home;
