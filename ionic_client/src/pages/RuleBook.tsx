import { IonContent, IonHeader, IonPage, IonTitle, IonToolbar } from '@ionic/react';
import React, { Component } from "react";
import { RouteComponentProps } from "react-router";
import './RuleBook.css';

type RuleBookParams = { rule : string };

type RuleBookProps = { root : boolean } & RouteComponentProps<RuleBookParams>;

type RuleBookState = {};

class RuleBook extends Component<RuleBookProps, RuleBookState> {
  componentDidMount() {
    var gl_element = document.getElementById("rulebook")!;
    if(this.props.root) {
      gl_element.innerHTML = "root";
    } else {
      gl_element.innerHTML = this.props.match.params.rule;
    }
    console.log(this.props.match.params.rule);
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
