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
| `control_used` | `trigger`: `global_hotkey`, `window_shortcut`, `tray`, `media_controls`, `taskbar`, `dock` (macOS) — once a run per trigger |
| `update` | `action`: `offered` (with `kind` `auto`/`manual`), `install`, `install_launched`, `failed`, `release_page` |
| `star_prompt` | `action`: `shown`, `star`, `dismiss` |
| `listening_time` | `minutes` of actual playback, sent hourly and at quit |

Every event also carries the app version, and `app_launch` the OS, its
version and the CPU architecture (`os`, `os_version`, `arch`). `source` is a category for
built-in sources (`yandex_music`, `youtube_music`, `local_folder`) and
`other` for the rest. Requests do not contain track or playlist names or
IDs, error messages, account details, or arbitrary backend data. Each
request also carries the preferred system UI language (e.g. `ru-ru`), the primary
screen's size, and a `User-Agent` naming the OS and the app, e.g.
`Mozilla/5.0 (X11; Linux x86_64) CloudMus/0.2.0`, and the same as
user-agent client hints (`uap`, `uapv`, `uaa`, `uab`, `uafvl`), which is
what GA4 reads the OS, OS version and architecture from. The device model
(`uam`) is not sent: a desktop has no portable one. Requests go directly
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
`sr`, `dl`, `dt`, `npa`, `uap`, `uapv`, `uaa`, `uab`, `uamb`, `uam`, `uafvl`, `uaw`, and per event `en`, `_et`, `ep.*`, `epn.*`). A
single event goes in the URL; several go in the body, one per line.

## Setting up a build

Create a GA4 property with a Web data stream and set its measurement ID in
`CLOUDMUS_GA4_MEASUREMENT_ID` before configuring CMake or running
`build-appimage.sh`. The release workflow reads a GitHub Actions secret
with that name and fails if it is missing. The ID ends up in the public
AppImage, so anyone can send events to the property — as with any website's
tag. In GA4 Custom definitions, register event-scoped dimensions for
`source`, `kind`, `action`, `reason`, `trigger`, `theme`, `glass`,
`ui_language`, `at_login`, `hidden`, `app_version`, `os`, `os_version` and
`arch`, plus custom metrics
for `saved_count`, `count` and `minutes`, to report on those event
parameters.

## Setting up the GA4 property by hand

Nothing below can be done from the repository. Do it once per property;
data arrives only from the moment a definition exists, and a new
dimension takes up to about 24 hours to appear in reports.

1. **Create the property and a stream.** In
   [analytics.google.com](https://analytics.google.com) → Admin → Create →
   Property. Then Admin → Data collection and modification → Data streams
   → Add stream → **Web** (the app speaks the web tag's protocol, so an
   iOS/Android stream does not fit). Any URL works, e.g.
   `https://github.com/cloudmus/cloudmus`. Copy the **Measurement ID**
   (`G-XXXXXXXXXX`) into the `CLOUDMUS_GA4_MEASUREMENT_ID` GitHub Actions
   secret (see "Setting up a build").
2. **Leave Enhanced measurement as it is.** It only matters for web pages;
   the app sends its own events.
3. **Register custom dimensions.** Admin → Data display → Custom
   definitions → **Custom dimensions** → Create custom dimension. For
   each row of the first table: Scope **Event**, Dimension name as shown,
   Event parameter exactly as in the second column (case-sensitive).

   | Dimension name | Event parameter |
   |---|---|
   | Source | `source` |
   | Kind | `kind` |
   | Action | `action` |
   | Reason | `reason` |
   | Trigger | `trigger` |
   | Theme | `theme` |
   | Glass | `glass` |
   | UI language | `ui_language` |
   | At login | `at_login` |
   | Hidden | `hidden` |
   | App version | `app_version` |
   | OS | `os` |
   | OS version | `os_version` |
   | CPU architecture | `arch` |

   Without this, these parameters are collected but never show up in
   reports or Explorations. The free tier allows 50 event-scoped
   dimensions, so the 14 above fit.
4. **Register custom metrics.** Same page → **Custom metrics** → Create
   custom metric. Scope **Event**, Unit of measurement **Standard**
   (for `minutes` choose **Minutes**):

   | Metric name | Event parameter |
   |---|---|
   | Saved count | `saved_count` |
   | Crash count | `count` |
   | Listening minutes | `minutes` |
5. **Mark key events (optional).** Admin → Data display → Events → turn
   on "Mark as key event" for `playback_started`, `download_completed`
   and `star_prompt` if you want conversions for them. A key event is a
   whole event name, so `star_prompt` counts shown, starred and
   dismissed together; to count only stars, use the `action` dimension in
   an Exploration instead.
6. **Keep the data retention at 14 months.** Admin → Data collection and
   modification → Data retention → Event data retention → 14 months
   (the default is 2), or Explorations will not reach past two months.
7. **Don't turn on Google signals or ad personalization.** The app sends
   `npa=1`, and the Settings checkbox promises plain usage statistics.
8. **Find the data.**
   - OS, OS version, architecture, language, country: Reports → Tech →
     Tech details, and Reports → Demographics. These come from the client
     hints and need no registration. Change the primary dimension in the
     table to "Operating system", "Operating system version" or
     "Platform / device category". The device model stays "(not set)": the
     app does not send it.
   - App version, OS (`os` and friends), actions: Explore → Blank →
     Dimensions: add the registered ones; Metrics: Event count, Active
     users; put e.g. "App version" in Rows and "Event count" in Values,
     filter "Event name" exactly matching `app_launch`.
   - GitHub star prompt: an Exploration with "Event name" = `star_prompt`,
     rows "Action" (`shown`, `star`, `dismiss`), values "Event count".
     The ratio `star` / `shown` is the conversion.
   - Downloads: the same with `download_completed`, rows "Kind" and
     "Source", values "Event count" and "Saved count".
9. **Check it works.** Admin → DebugView, with the app started under
   `CLOUDMUS_QT_DEBUG=1` (see "Checking it"). Realtime (Reports →
   Realtime) shows events within a minute, but DebugView shows their
   parameters. A parameter you see in DebugView but not in reports means
   its custom dimension is missing or still propagating.

## Checking it

Start the Qt front with `CLOUDMUS_QT_DEBUG=1` (or `--debug`). Its
`~/.local/state/cloudmus/fronts/qt/debug.log` records analytics initialization
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
`sentry-cli debug-files upload --include-sources`, so stacks in Sentry get
function names and lines, and each frame shows the source around its line
(our own code and sentry-native's). The sources are found by the paths in the
debug info, `/workspace/...` — the checkout's place in the build container;
the Windows build rewrites its `Z:/workspace` to that
(`-ffile-prefix-map` in `packaging/windows/build-in-docker.sh`), and the
upload step symlinks `/workspace` to the runner's checkout. That needs the
GitHub secrets `SENTRY_AUTH_TOKEN` (an organization token with the
`project:releases` scope) and `SENTRY_ORG` (the organization slug); the
project is `cloudmus-qt`. Symbols are matched by build id, so
upload them for every release build.

## Checking it

Crash a build that has a DSN — `kill -SEGV <pid>` of a running
`cloudmus-qt` — then start it again and look for the event in the project's
Issues. The release is `cloudmus-qt@<version>`; the environment is
`production` for a tagged release and `development` for any other build.
