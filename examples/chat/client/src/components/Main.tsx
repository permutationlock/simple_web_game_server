import React from 'react';
import { Switch, Route } from 'react-router-dom';

import Home from '../pages/Home';
import Room from '../pages/Room';

const Main = () => {
  return (
    <Switch>
      <Route exact path='/' component={Home}></Route>
      <Route path='/room/:session/:name' component={Room}></Route>
    </Switch>
  );
}

export default Main;
