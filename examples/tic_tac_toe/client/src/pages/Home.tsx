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
    const login_uri = "http://localhost:9092/login";

    fetch(login_uri)
      .then(response => response.text())
      .then((token_str) => {
        console.log('login token: ' + token_str);
        this.setState({ token: token_str, disabled: false });
      });
  }

  handleClick() {
    this.props.history.push("/match/" + this.state.token);
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
