// Service worker for UberSDR Antenna Switch PWA.
// Strategy: network-first, no caching.
// This is a local device app — we always want the live page, never a stale cache.
// The service worker exists solely to satisfy the PWA installability requirement.

const CACHE_NAME = "ubersdr-v1";

self.addEventListener("install", function(e) {
  // Skip waiting so the SW activates immediately.
  self.skipWaiting();
});

self.addEventListener("activate", function(e) {
  // Take control of all clients immediately.
  e.waitUntil(self.clients.claim());
  // Remove any old caches from previous versions.
  e.waitUntil(
    caches.keys().then(function(keys) {
      return Promise.all(
        keys.filter(function(k) { return k !== CACHE_NAME; })
            .map(function(k) { return caches.delete(k); })
      );
    })
  );
});

self.addEventListener("fetch", function(e) {
  // Always go to the network. If the network fails (device offline/rebooting),
  // return a simple offline message rather than a cached stale page.
  e.respondWith(
    fetch(e.request).catch(function() {
      return new Response(
        "<html><body style='font-family:sans-serif;text-align:center;padding:40px;background:#0b0f14;color:#8695a6'>" +
        "<h2 style='color:#38bdf8'>Antenna Switch</h2>" +
        "<p>Device is offline or rebooting&hellip;</p>" +
        "<p><a href='/' style='color:#38bdf8'>Retry</a></p>" +
        "</body></html>",
        { headers: { "Content-Type": "text/html" } }
      );
    })
  );
});
