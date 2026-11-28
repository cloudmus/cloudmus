// The app's two requests, over one port:
//
//   {type: "connection", connection}  go out as the source does (docs/protocol.md
//       §4.3), before anything loads; answered {type: "connected"}, or
//       {type: "refused"} if that can't be done: nothing may go past a proxy.
//   {type: "cookies", urls}  answered {type: "cookies", cookies: {url:
//       "name=value; ..."}}, httpOnly ones included, which no page script sees.

let route = null;
let credentials = null;
// Requests whose proxy has had the credentials once: asked again, they're wrong.
const tried = new Set();

function connectThrough(connection) {
  const mode = connection ? connection.mode : "";
  route = null;
  credentials = null;
  if (mode === "direct") {
    route = {type: "direct"};
  } else if (mode === "proxy") {
    const socks = connection.type === "socks5";
    route = {type: socks ? "socks" : "http", host: connection.host, port: connection.port};
    if (socks)
      Object.assign(route, {proxyDNS: true, username: connection.username || "", password: connection.password || ""});
    else if (connection.username)
      credentials = {username: connection.username, password: connection.password || ""};
  }
  if (route && !browser.proxy.onRequest.hasListener(chooseRoute)) {
    browser.proxy.onRequest.addListener(chooseRoute, {urls: ["<all_urls>"]});
    browser.webRequest.onAuthRequired.addListener(authenticate, {urls: ["<all_urls>"]}, ["blocking"]);
  }
}

// The null after it: no falling back to the system's way if the proxy fails.
function chooseRoute() {
  return route && route.type !== "direct" ? [route, null] : route;
}

function authenticate(details) {
  if (!details.isProxy || !credentials)
    return {};
  if (tried.has(details.requestId))
    return {cancel: true};
  tried.add(details.requestId);
  return {authCredentials: credentials};
}

async function cookiesOf(urls) {
  const cookies = {};
  for (const url of urls || []) {
    try {
      const found = await browser.cookies.getAll({url});
      cookies[url] = found.map((c) => c.name + "=" + c.value).join("; ");
    } catch (error) {
      cookies[url] = "";
    }
  }
  return cookies;
}

// Connects again when the app's end goes away, or wasn't listening yet when
// this script started.
function connect() {
  const port = browser.runtime.connectNative("cloudmus");
  port.onDisconnect.addListener(() => setTimeout(connect, 1000));
  port.onMessage.addListener(async (message) => {
    if (message.type === "connection") {
      try {
        connectThrough(message.connection);
        port.postMessage({type: "connected"});
      } catch (error) {
        port.postMessage({type: "refused", error: String(error)});
      }
    } else if (message.type === "cookies") {
      port.postMessage({type: "cookies", cookies: await cookiesOf(message.urls)});
    }
  });
}
connect();
