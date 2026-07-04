// Service worker for UberSDR Antenna Switch PWA.
//
// Strategy:
//   - Static assets (icons, manifest, favicon) are cached on install and served
//     from cache on subsequent loads — the ESP8266 only serves them once.
//   - Everything else (the app page, all /api/* calls) is network-first so the
//     UI always reflects live device state. Falls back to an offline message if
//     the device is unreachable.

const CACHE_NAME = "ubersdr-v2";

// Static assets that never change between firmware updates.
// Cached on SW install so the ESP8266 is not hit on every page load.
const STATIC_ASSETS = [
  "/favicon.ico",
  "/icon-192.png",
  "/icon-512.png",
  "/manifest.json"
];

self.addEventListener("install", function(e) {
  e.waitUntil(
    caches.open(CACHE_NAME).then(function(cache) {
      return cache.addAll(STATIC_ASSETS);
    })
  );
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
  var url = new URL(e.request.url);

  // Serve static assets from cache (cache-first).
  if(STATIC_ASSETS.indexOf(url.pathname) !== -1) {
    e.respondWith(
      caches.match(e.request).then(function(cached) {
        return cached || fetch(e.request);
      })
    );
    return;
  }

  // Everything else: network-first. If the network fails (device offline /
  // rebooting), return a simple offline message rather than a stale page.
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
