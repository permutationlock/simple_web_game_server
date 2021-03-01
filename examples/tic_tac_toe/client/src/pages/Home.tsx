import React from 'react';
import { RouteComponentProps } from 'react-router-dom';
import './Home.css';

type HomeState = { token: string, disabled: boolean };
type HomeProps = RouteComponentProps<any>;

class Home extends React.Component<HomeProps, HomeState> {
   
  state = {
    token: "",
    disabled: true
  };

  componentDidMount() {
    const signupUri = "https://localhost:9092/signup";

    const token = sessionStorage.getItem("token");
    if(token == null) {
      fetch(signupUri)
        .then(response => response.text())
        .then((tokenStr) => {
          console.log('player token: ' + tokenStr);
          sessionStorage.setItem("token", tokenStr);
          this.setState({ token: tokenStr, disabled: false });
        });
    } else {
      console.log('player token: ' + token);
      this.setState({ token: token, disabled: false });
    }
  }

  handleClick() {
    const loginUri = "https://localhost:9092/login/" + this.state.token;

    fetch(loginUri)
      .then(response => response.text())
      .then((tokenStr) => {
        console.log('match request token: ' + tokenStr);
        this.props.history.push("/match/" + tokenStr);
      });
  }

  render() {
    return (
      <div className='Home'>
        <button disabled={this.state.disabled}
          onClick={this.handleClick.bind(this)} id="Match-button">
          Match
        </button>
      </div>
    );
  }
};

export default Home;
