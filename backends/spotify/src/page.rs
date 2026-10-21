//! The page the browser lands on at the end of a sign-in, served by the
//! redirect listener (account.rs). It looks like https://cloudmus.app: the
//! same dark warm background with drifting glows, Rubik, the logo, the
//! orange accent. Everything is inline — fonts included — since the page
//! must show without the network and comes from a listener that answers
//! this one request and closes.

use base64::engine::general_purpose::STANDARD as BASE64;
use base64::Engine as _;

const LOGO: &str = include_str!("../assets/logo.svg");
const RUBIK_LATIN: &[u8] = include_bytes!("../assets/fonts/rubik-latin.woff2");
const RUBIK_CYRILLIC: &[u8] = include_bytes!("../assets/fonts/rubik-cyrillic.woff2");

/// How the sign-in ended, for the badge on the logo.
#[derive(Clone, Copy)]
pub enum Outcome {
    SignedIn,
    Cancelled,
}

fn escape(text: &str) -> String {
    text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

/// The HTML page: `title` and `text` are plain text (escaped here).
pub fn html(outcome: Outcome, lang: &str, title: &str, text: &str) -> String {
    let (badge_class, badge_icon) = match outcome {
        Outcome::SignedIn => ("ok", r#"<path d="M5 12.5l4.2 4.2L19 7"/>"#),
        Outcome::Cancelled => ("off", r#"<path d="M7 7l10 10M17 7L7 17"/>"#),
    };
    let latin = BASE64.encode(RUBIK_LATIN);
    let cyrillic = BASE64.encode(RUBIK_CYRILLIC);
    let (title, text, lang) = (escape(title), escape(text), escape(lang));
    format!(
        r#"<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title} — CloudMus</title>
<style>
@font-face{{font-family:Rubik;font-weight:400 800;src:url(data:font/woff2;base64,{latin}) format('woff2');unicode-range:U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD}}
@font-face{{font-family:Rubik;font-weight:400 800;src:url(data:font/woff2;base64,{cyrillic}) format('woff2');unicode-range:U+0301,U+0400-045F,U+0490-0491,U+04B0-04B1,U+2116}}
:root{{color-scheme:dark}}
*{{box-sizing:border-box}}
html,body{{margin:0;min-height:100%;background:#15110f;color:#f3ede9;font-family:Rubik,system-ui,sans-serif;-webkit-font-smoothing:antialiased}}
body{{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;overflow:hidden;position:relative}}
.orb{{position:absolute;border-radius:50%;pointer-events:none}}
.o1{{width:55vw;height:55vw;left:-15vw;top:-12vw;background:radial-gradient(closest-side,rgba(255,90,43,.28),transparent);animation:d1 18s ease-in-out infinite}}
.o2{{width:45vw;height:45vw;right:-12vw;bottom:-14vw;background:radial-gradient(closest-side,rgba(150,60,110,.22),transparent);animation:d2 22s ease-in-out infinite}}
.o3{{width:34vw;height:34vw;left:40vw;top:55vh;background:radial-gradient(closest-side,rgba(255,140,60,.12),transparent);animation:d3 26s ease-in-out infinite}}
@keyframes d1{{50%{{transform:translate(8vw,6vh) scale(1.15)}}}}
@keyframes d2{{50%{{transform:translate(-10vw,-4vh) scale(.9)}}}}
@keyframes d3{{50%{{transform:translate(4vw,-8vh)}}}}
@media (prefers-reduced-motion:reduce){{.orb{{animation:none}}}}
.card{{position:relative;z-index:1;width:100%;max-width:520px;text-align:center;padding:clamp(32px,6vw,52px) clamp(24px,6vw,52px);border-radius:24px;background:rgba(30,24,21,.55);border:1px solid rgba(255,255,255,.08);backdrop-filter:blur(20px) saturate(1.4);-webkit-backdrop-filter:blur(20px) saturate(1.4);box-shadow:0 30px 80px -30px rgba(0,0,0,.6)}}
.mark{{position:relative;width:104px;height:104px;margin:0 auto 26px}}
.mark svg.logo{{width:104px;height:104px;display:block;filter:drop-shadow(0 14px 34px rgba(255,90,43,.35))}}
.badge{{position:absolute;right:-6px;bottom:-6px;width:38px;height:38px;border-radius:50%;display:flex;align-items:center;justify-content:center;border:4px solid #1e1815}}
.badge svg{{width:20px;height:20px;fill:none;stroke:#fff;stroke-width:2.6;stroke-linecap:round;stroke-linejoin:round}}
.badge.ok{{background:#ff5a2b;box-shadow:0 8px 24px -6px rgba(255,90,43,.7)}}
.badge.off{{background:#4a3f39}}
h1{{margin:0;font-weight:800;font-size:clamp(28px,5.2vw,40px);line-height:1.08;letter-spacing:-.03em;text-wrap:balance}}
p{{margin:14px auto 0;max-width:380px;font-size:17px;line-height:1.5;color:rgba(243,237,233,.7);text-wrap:balance}}
.brand{{margin-top:32px;font-weight:700;font-size:15px;letter-spacing:-.01em;color:rgba(243,237,233,.45)}}
.brand b{{color:#ff5a2b;font-weight:700}}
</style>
</head>
<body>
<div class="orb o1"></div><div class="orb o2"></div><div class="orb o3"></div>
<main class="card">
<div class="mark">{logo}<span class="badge {badge_class}"><svg viewBox="0 0 24 24" aria-hidden="true">{badge_icon}</svg></span></div>
<h1>{title}</h1>
<p>{text}</p>
<div class="brand">Cloud<b>Mus</b></div>
</main>
</body>
</html>
"#,
        logo = LOGO.trim().replacen("<svg ", r#"<svg class="logo" aria-hidden="true" "#, 1),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn page_carries_its_text_escaped_and_needs_nothing_from_outside() {
        let page = html(Outcome::SignedIn, "ru", "Вход <выполнен>", "Можно закрыть & вернуться");
        assert!(page.contains("<h1>Вход &lt;выполнен&gt;</h1>"));
        assert!(page.contains("Можно закрыть &amp; вернуться"));
        assert!(page.contains(r#"<html lang="ru">"#));
        assert!(page.contains("badge ok"));
        assert!(page.contains("data:font/woff2;base64,"));
        for external in ["src=\"http", "href=\"http", "url(http", "@import"] {
            assert!(!page.contains(external), "no external resources: {external}");
        }
        assert!(html(Outcome::Cancelled, "en", "x", "y").contains("badge off"));
    }
}
