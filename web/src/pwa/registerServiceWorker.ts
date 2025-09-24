// Copyright (c) 2025 Кочуров Владислав Евгеньевич

function resolveBasePath(): string {
  if (typeof window === "undefined") {
    return "/";
  }

  const resolved = new URL(import.meta.env.BASE_URL ?? "/", window.location.href);
  const pathname = resolved.pathname.endsWith("/") ? resolved.pathname : `${resolved.pathname}/`;
  return pathname;
}

export function registerServiceWorker(): void {
  if (import.meta.env.DEV) {
    return;
  }

  if (typeof window === "undefined" || !("serviceWorker" in navigator)) {
    return;
  }

  const basePath = resolveBasePath();
  const serviceWorkerPath = `${basePath}service-worker.js`;

  const register = () => {
    navigator.serviceWorker
      .register(serviceWorkerPath, { scope: basePath })
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
