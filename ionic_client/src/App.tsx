import React from 'react';
import { Redirect, Route } from 'react-router-dom';
import {
  IonApp,
  IonLabel,
  IonRouterOutlet,
  IonTabBar,
  IonTabButton,
  IonTabs
} from '@ionic/react';
import { IonReactRouter } from '@ionic/react-router';
import Game from './pages/Game';
import RuleBook from './pages/RuleBook';

/* Core CSS required for Ionic components to work properly */
import '@ionic/react/css/core.css';

/* Basic CSS for apps built with Ionic */
import '@ionic/react/css/normalize.css';
import '@ionic/react/css/structure.css';
import '@ionic/react/css/typography.css';

/* Optional CSS utils that can be commented out */
import '@ionic/react/css/padding.css';
import '@ionic/react/css/float-elements.css';
import '@ionic/react/css/text-alignment.css';
import '@ionic/react/css/text-transformation.css';
import '@ionic/react/css/flex-utils.css';
import '@ionic/react/css/display.css';

/* Theme variables */
import './theme/variables.css';

var ws_path = 'ws://localhost:9090';
var gsocket = new WebSocket(ws_path);

const App: React.FC = () => (
  <IonApp>
    <IonReactRouter>
      <IonTabs>
        <IonRouterOutlet>
          <Route path="/game" exact={true}
            render={(props) => ( <Game {...props} socket={gsocket} /> )}/>
          <Route path="/rulebook/:rule"
            render={(props) => ( <RuleBook {...props} root={false} /> )} />
          <Route path="/rulebook/" exact={true}
            render={(props) => ( <RuleBook {...props} root={true} /> )} />

          <Route exact path="/" render={() => <Redirect to="/game" />} />
        </IonRouterOutlet>
        <IonTabBar slot="bottom">
          <IonTabButton tab="game" href="/game">
            <IonLabel>Game</IonLabel>
          </IonTabButton>
          <IonTabButton tab="rules" href="/rulebook">
            <IonLabel>Rules</IonLabel>
          </IonTabButton>
        </IonTabBar>
      </IonTabs>
    </IonReactRouter>
  </IonApp>
);

export default App;
