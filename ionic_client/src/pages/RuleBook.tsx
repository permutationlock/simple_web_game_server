import { IonContent, IonHeader, IonPage, IonTitle, IonToolbar } from '@ionic/react';
import React, { Component } from "react";
import { RouteComponentProps } from "react-router";
import './RuleBook.css';

class RuleBook extends Component<RouteComponentProps, {}> {
    
    componentDidMount() {
        var gl_element = document.getElementById("rulebook")!;
        gl_element.innerHTML = this.props.match.path;
        console.log(this.props.match);
    }

    render() {
        return (
            <IonPage>
              <IonHeader>
                <IonToolbar>
                  <IonTitle>Rule Book</IonTitle>
                </IonToolbar>
              </IonHeader>
              <IonContent fullscreen>
                <div id="rulebook"></div>
              </IonContent>
            </IonPage>
        )
    }
}

export default RuleBook;
