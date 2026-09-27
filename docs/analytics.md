# Usage statistics in the Qt front

The Qt front sends usage events to Google Analytics when built with GA4
credentials. This is on by default and can be turned off under **Settings →
General → Send usage statistics to Google Analytics**. Turning it off stops
new requests immediately; turning it back on keeps the same random
installation ID, which is stored in the Qt front's `config.ini`.

Events cover app launches, playback starts, opening a source, completed
downloads, and adding or removing a track from a playlist. Requests contain
the app version, a category for built-in sources, and where relevant the
download type/count or playlist action. They do not contain track or playlist
names or IDs, account details, or arbitrary backend data. Requests go
directly from the app to Google over HTTPS. Google can also see the network
address used for the request. The app also sends the preferred system UI
language, the operating system (Windows and macOS with their version), the
primary screen's size, the app name and version as the GA4 browser fields,
and, when available, a country, region, continent and city. It looks up the public IP's
location through `https://ipwho.is/` first and `http://ip-api.com/json/` if
that fails. The second service uses plain HTTP and its free API is limited to
non-commercial use (https://ip-api.com/docs/legal). The last successful
location is kept in the Qt front's `config.ini` for use when both
services fail. Geo lookups run at startup and after network interface changes,
and stop when usage statistics are disabled. If no location is available,
events omit it. GA4 Measurement Protocol does not guarantee that it will
infer location from the request IP in that case; see
https://developers.google.com/analytics/devguides/collection/protocol/ga4/reference?client_type=gtag#geographic_information.
The app does not set a custom `User-Agent`; it sends the language in the GA4
`device.language` field as a two-letter language code with an optional region
(for example, Qt's `ru-Cyrl-RU` becomes `ru-RU`). Events still queued when
the app quits get up to two seconds to go out, sent with the saved location
if a lookup is still running. Builds without GA4 credentials send nothing.

To enable this in a build, create a GA4 property with a Web data stream and
a Measurement Protocol API secret. Set `CLOUDMUS_GA4_MEASUREMENT_ID` and
`CLOUDMUS_GA4_API_SECRET` in the environment before configuring CMake or
running `build-appimage.sh`. The release workflow reads GitHub Actions
secrets with those names and fails if either is missing. The API secret is
embedded in the public AppImage and can be extracted; someone who obtains
it could send false events to the GA4 property. In GA4 Custom definitions,
register event-scoped dimensions for `source`, `kind`, `action`, and
`app_version`, plus a custom metric for `saved_count`, to report on those
event parameters.

For a local GA4 check, start the Qt front with `CLOUDMUS_QT_DEBUG=1`. Its
`~/.config/cloudmus/fronts/qt/debug.log` records analytics initialization,
the event JSON sent in each request, and the HTTP status or network error.
The geo lookup log also shows which service was tried, its result, and when
the app falls back to another service or a saved location.
In this mode each request body is also posted to GA4's validation server
(`/debug/mp/collect`, which records nothing), and the log shows its
`validationMessages`. That server checks only event names and parameters;
it accepts any `user_location` or `device` values, so a clean result says
nothing about them.
The installation ID and API secret are redacted. An HTTP 2xx means Google's
endpoint received the request; it does not guarantee GA4 accepted every
event, so also check the GA4 Realtime report.
