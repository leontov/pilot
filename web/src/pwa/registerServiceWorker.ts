export function registerServiceWorker(): void {
  if (import.meta.env.DEV) {
    return;
  }

  if (typeof window === "undefined" || !("serviceWorker" in navigator)) {
    return;
  }

  const register = () => {
    navigator.serviceWorker
      .register("/service-worker.js")
      .catch((error) => {
        console.error("Service worker registration failed", error);
      });
  };

  if (document.readyState === "complete") {
    register();
  } else {
    window.addEventListener("load", register, { once: true });
  }
}
