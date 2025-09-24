// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import "./styles/index.css";
import { registerServiceWorker } from "./pwa/registerServiceWorker";

const container = document.getElementById("app");

if (!container) {
  throw new Error("Не удалось найти корневой элемент приложения");
}

ReactDOM.createRoot(container).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);

registerServiceWorker();
