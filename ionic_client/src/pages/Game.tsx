import { IonContent, IonHeader, IonPage, IonTitle, IonToolbar } from '@ionic/react';
import React, { Component } from "react";
import * as THREE from "three";
import './Game.css';

type GameProps = {
  socket :WebSocket
};

type GameState = {
};

class GameThree {
  scene : THREE.Scene;
  camera : THREE.PerspectiveCamera;
  renderer : THREE.WebGLRenderer;

  constructor(width : number, height : number) {
	this.scene = new THREE.Scene();
	this.camera = new THREE.PerspectiveCamera(
	  75, width/height, 0.1, 1000 );
	this.renderer = new THREE.WebGLRenderer();

	this.renderer.setSize(width, height);
  }

  resize(width : number, height : number) {
	this.camera.aspect = width / height;
	this.camera.updateProjectionMatrix()
	this.renderer.setSize(width, height);
  }
}

class Game extends Component<GameProps, GameState> {
	gameThree : GameThree;

	constructor(props : GameProps) {
	  super(props);
	  this.gameThree = new GameThree(10, 10);
	}

	componentDidMount() {
		var gl_element = document.getElementById("webgl-container")!;

		gl_element.appendChild(this.gameThree.renderer.domElement);

		var geometry = new THREE.BoxGeometry(1, 1, 1 );
		var material = new THREE.MeshBasicMaterial({ color: 0x00ff00 });
		var cube = new THREE.Mesh(geometry, material);
		cube.position.set(0.5, 0.5, 0.5);

		this.gameThree.scene.add(cube);
		this.gameThree.camera.position.z = 9;

		var animate = () => {
			requestAnimationFrame(animate);
			//cube.rotation.x += 0.05;
			//cube.rotation.y += 0.05;
			this.gameThree.renderer.render(
				this.gameThree.scene,
				this.gameThree.camera
			  );
		};
		animate();
    
    window.addEventListener('resize', this.checkCanvasSize.bind(this));

    this.checkCanvasSize();
	}

  // Wait for the dom to be rendered and size canvas appropriately
	checkCanvasSize() { 
	  var gl_element = document.getElementById("webgl-container")!;
	  let width = gl_element.offsetWidth;
	  let height = gl_element.offsetHeight; 

    if(gl_element.offsetWidth) {
      this.gameThree.resize(width, height);
    }
    else {
      setTimeout(this.checkCanvasSize.bind(this), 100);	 
    }
	}

	componentDidUpdate() { 
    this.checkCanvasSize();
	}

	render() {
		return (
			<IonPage>
				<IonHeader>
					<IonToolbar>
						<IonTitle>Three Scene</IonTitle>
					</IonToolbar>
				</IonHeader>
				<IonContent fullscreen>
					<div id="webgl-container"></div>
				</IonContent>
			</IonPage>
		)
	}
}

export default Game;
