import React from 'react';
import { Switch, Route } from 'react-router-dom';

import Home from '../pages/Home';
import Match from '../pages/Match';
import Game from '../pages/Game';

const Main = () => {
  return (
    <Switch>
      <Route exact path='/' component={Home}></Route>
      <Route path='/match/:token' component={Match}></Route>
      <Route path='/game/:token' component={Game}></Route>
    </Switch>
  );
}

export default Main;
