import { IonContent, IonHeader, IonPage, IonTitle, IonToolbar } from '@ionic/react';
import React, { Component } from "react";
import * as THREE from "three";
import './Home.css';

class Home extends Component {
    componentDidMount() {
        var scene = new THREE.Scene();
        var camera = new THREE.PerspectiveCamera(
                75, window.innerWidth/window.innerHeight, 0.1, 1000 );
        var renderer = new THREE.WebGLRenderer();

        renderer.setSize(window.innerWidth, window.innerHeight);

        var gl_element = document.getElementById("gl_render")!;
        gl_element.appendChild(renderer.domElement);

        var geometry = new THREE.BoxGeometry(1, 1, 1 );
        var material = new THREE.MeshBasicMaterial({ color: 0x00ff00 });
        var cube = new THREE.Mesh(geometry, material);

        scene.add(cube);
        camera.position.z = 5;

        var animate = function () {
            requestAnimationFrame(animate);
            cube.rotation.x += 0.05;
            cube.rotation.y += 0.05;
            renderer.render(scene, camera);
        };
        animate();
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
                <div id="gl_render"></div>
              </IonContent>
            </IonPage>
        )
    }
}

export default Home;
