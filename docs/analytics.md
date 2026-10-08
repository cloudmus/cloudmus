# Usage statistics and crash reports in the Qt front

The Qt front sends usage events to Google Analytics when built with a GA4
measurement ID. This is on by default and can be turned off under
**Settings → General → Send usage statistics to Google Analytics**. Turning
it off stops new requests immediately; turning it back on keeps the same
random installation ID, which is stored in the Qt front's `config.ini`
along with a count of the app's runs (GA4's session number).

Events and their parameters:

| Event | Parameters |
|---|---|
| `app_launch` | `at_login`, `hidden` (started into the tray), `theme`, `glass`, `ui_language` (the one picked in Settings, or `system`) |
| `app_crashed` | `count`: crash reports the previous runs left |
| `playback_started`, `source_opened` | `source` |
| `playback_failed` | `source`, `reason`: `unavailable`, `rejected`, `stream`, `timeout`, `station_empty` |
| `backend_failed` | `source`, `reason`: `crashed` (restarted) or `gave_up` |
| `sign_in` | `source`, `action`: `prompt`, `success`, `error` |
| `download_completed` | `source`, `kind`, `saved_count` |
| `playlist_changed` | `source`, `action`: `add`, `remove` |
| `track_feedback` | `source`, `action`: `like`, `unlike`, `dislike`, `undislike` |
| `control_used` | `trigger`: `global_hotkey`, `window_shortcut`, `tray`, `media_controls`, `taskbar` — once a run per trigger |
| `update` | `action`: `offered` (with `kind` `auto`/`manual`), `install`, `install_launched`, `failed`, `release_page` |
| `star_prompt` | `action`: `shown`, `star`, `dismiss` |
| `listening_time` | `minutes` of actual playback, sent hourly and at quit |

Every event also carries the app version. `source` is a category for
built-in sources (`yandex_music`, `youtube_music`, `local_folder`) and
`other` for the rest. Requests do not contain track or playlist names or
IDs, error messages, account details, or arbitrary backend data. Each
request also carries the preferred system UI language (e.g. `ru-ru`), the primary
screen's size, and a `User-Agent` naming the OS and the app, e.g.
`Mozilla/5.0 (X11; Linux x86_64) CloudMus/0.2.0`. Requests go directly
from the app to Google over HTTPS; Google derives an approximate location
(country, region, city) from the request's network address. Events still
queued when the app quits get up to two seconds to go out. Builds without a
measurement ID send nothing.

## Why the web tag's endpoint

The app sends to `https://www.google-analytics.com/g/collect`, the endpoint
GA4's web tag (gtag.js) uses, in the same format — not to the Measurement
Protocol (`/mp/collect`). The Measurement Protocol is meant for servers: it
doesn't derive a location from the request's address, and without one sent
explicitly every user showed up in the middle of the Atlantic ("(not set)")
even with `user_location` filled in from an IP lookup. `/g/collect` needs no
API secret either. The catch: it isn't a documented API, so Google may
change it; the parameters used are the ones gtag.js sends
(`v`, `tid`, `cid`, `sid`, `sct`, `seg`, `_s`, `_ss`, `_nsi`, `_fv`, `ul`,
`sr`, `dl`, `dt`, `npa`, and per event `en`, `_et`, `ep.*`, `epn.*`). A
single event goes in the URL; several go in the body, one per line.

## Setting up a build

Create a GA4 property with a Web data stream and set its measurement ID in
`CLOUDMUS_GA4_MEASUREMENT_ID` before configuring CMake or running
`build-appimage.sh`. The release workflow reads a GitHub Actions secret
with that name and fails if it is missing. The ID ends up in the public
AppImage, so anyone can send events to the property — as with any website's
tag. In GA4 Custom definitions, register event-scoped dimensions for
`source`, `kind`, `action`, `reason`, `trigger`, `theme`, `glass`,
`ui_language`, `at_login`, `hidden` and `app_version`, plus custom metrics
for `saved_count`, `count` and `minutes`, to report on those event
parameters.

## Checking it

Start the Qt front with `CLOUDMUS_QT_DEBUG=1` (or `--debug`). Its
`~/.config/cloudmus/fronts/qt/debug.log` records analytics initialization
with the `User-Agent`, each request's parameters and events, and the HTTP
status or network error; the installation ID is redacted. In this mode
events are also marked for GA4's DebugView (**Admin → DebugView**), which
shows them live. An HTTP 2xx means Google's endpoint received the request,
not that GA4 accepted every event, so check DebugView or the Realtime
report.

# Crash reports (Sentry)

When the Qt front crashes, a build made with a Sentry DSN also sends the
crash to Sentry. This is on by default and can be turned off under
**Settings → General → Send crash reports to Sentry**; the change applies
from the next start. Builds without a DSN, and runs with the setting off,
send nothing and keep only the local report (see `docs/development.md`).

A report holds the crash's stack (module and function names, offsets), the
app version, the OS and CPU, the loaded libraries, and the last 40 log lines
(a URL's query string is cut from them). It carries no account details or
user ID, and session tracking is off. The crash is stored on disk when it
happens and goes out at the next start, so nothing is sent from a process
that is already failing. Sentry sees the sender's network address; turn on
**Prevent Storing of IP Addresses** in the project's Security & Privacy
settings to drop it.

Turning the setting off also deletes events stored but not yet sent
(`crashes/sentry/` next to the local reports).

## Setting up a build

Set `CLOUDMUS_SENTRY_DSN` (the project's client key, from **Settings →
Client Keys**) before configuring CMake or running `build-appimage.sh` /
`build-windows.sh`; without it sentry-native is neither fetched nor linked.
The DSN ends up in the public binary, as the GA4 ID does. The release
workflow reads the secret of that name and fails if it is missing.

The release builds carry `-g1` debug info, and each build leaves its
unstripped executable in `dist/symbols/`. The workflow uploads it with
`sentry-cli debug-files upload`, so stacks in Sentry get function names and
lines. That needs the GitHub secrets `SENTRY_AUTH_TOKEN` (an organization
token with the `project:releases` scope) and `SENTRY_ORG` (the organization
slug); the project is `cloudmus-qt`. Symbols are matched by build id, so
upload them for every release build.

## Checking it

Crash a build that has a DSN — `kill -SEGV <pid>` of a running
`cloudmus-qt` — then start it again and look for the event in the project's
Issues. The release is `cloudmus-qt@<version>`; the environment is
`production` for a tagged release and `development` for any other build.
