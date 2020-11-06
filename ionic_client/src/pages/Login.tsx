import { IonContent, IonHeader, IonPage, IonTitle, IonToolbar } from '@ionic/react';
import React, { Component } from "react";
import './Home.css';

type HomeProps = {
  socket :WebSocket
};

class Home extends Component<HomeProps, HomeState> {
  constructor(props : HomeProps){
    super(props);
    this.state = { email: '', password: '', done: false };
  }

  componentDidMount() {
    
  }

  render() {
    return (
      <IonPage>
        <IonHeader>
          <IonToolbar>
            <IonTitle>Login</IonTitle>
          </IonToolbar>
        </IonHeader>
        <IonContent fullscreen>
          <div id="firebaseui-auth-container">
          </div>
        </IonContent>
      </IonPage>
    );
  }
}

export default Home;
