/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */

const CACHE_NAME = "kolibri-pwa-v1";
const scopeUrl = self.registration ? new URL(self.registration.scope) : new URL(self.location.href);
const basePath = scopeUrl.pathname.endsWith("/") ? scopeUrl.pathname : `${scopeUrl.pathname}/`;
const origin = scopeUrl.origin;
const BASE_URL = `${origin}${basePath}`;
const OFFLINE_URL = `${BASE_URL}offline.html`;
const MANIFEST_URL = `${BASE_URL}manifest.webmanifest`;
const PRECACHE_URLS = [BASE_URL, OFFLINE_URL, MANIFEST_URL];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches
      .open(CACHE_NAME)
      .then((cache) =>
        Promise.all(
          PRECACHE_URLS.map((url) =>
            cache.add(new Request(url, { cache: "reload" })).catch(() => undefined)
          )
        )
      )
      .then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) =>
        Promise.all(
          keys.map((key) => {
            if (key !== CACHE_NAME) {
              return caches.delete(key);
            }
            return undefined;
          })
        )
      )
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") {
    return;
  }

  const requestURL = new URL(event.request.url);

  if (requestURL.origin !== origin) {
    return;
  }

  if (event.request.mode === "navigate") {
    event.respondWith(
      fetch(event.request)
        .then((response) => {
          const copy = response.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copy));
          return response;
        })
        .catch(() => caches.match(OFFLINE_URL))
    );
    return;
  }

  event.respondWith(
    caches.match(event.request).then((cached) => {
      if (cached) {
        return cached;
      }
      return fetch(event.request)
        .then((response) => {
          const copy = response.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copy));
          return response;
        })
        .catch(() => {
          if (event.request.destination === "document") {
            return caches.match(OFFLINE_URL);
          }
          return Response.error();
        });
    })
  );
});
