# Changelog and releases

A release is an annotated `x.y.z` tag. Pushing it runs
`.github/workflows/release.yml`, which builds the AppImage and Windows NSIS
installer, checks the Windows package, and publishes both on the GitHub
release with the tag's message as its release notes. The app's
version (shown in the About dialog) comes from the same tag via
`git describe` (`fronts/qt/cmake/Version.cmake`).

## Changelog

`CHANGELOG.md` is seeded from git by `./update-changelog-from-git.sh`: it
inserts a `# X.X.X (<date>)` heading on top with one `- ` bullet per commit
subject since the last version in the file. `X.X.X` is replaced with the
real version when releasing.

### Tidying up the changelog

Edit only the section for commits since the last tagged release
(`git tag -l`) — typically the topmost, still-unreleased `# X.X.X` heading.
For each entry in that section:

- Drop entries of no interest to CloudMus users (people using the player)
  or backend/front authors (people relying on the protocol): routine
  `README`/`AGENTS.md` touch-ups, internal test/CI/build-tooling churn,
  typo fixes, reverted or rolled-back intermediate steps, and other commits
  that don't change user-visible behavior, a supported service's features,
  or the protocol.
- Refactors and source-tree reorganizations (moving files, renaming
  classes, replacing QSS with a custom `QStyle`, splitting modules) are not
  interesting on their own — drop them, unless they visibly change the
  app's look or behavior; then describe that change instead.
- Rephrase entries that describe an implementation step (a class name like
  `ThemedSlider`/`HeroPanel`, a file path, a `cloudmus-qt:` prefix) into a
  plain description of the resulting user-visible capability.
- Consolidate a run of incremental commits that build up one feature (e.g.
  several commits landing Yandex "My Wave") into a single bullet describing
  the feature's end state.
- Multiple commits touching the same component (the tray, notifications,
  menus, a specific backend such as Yandex Music) collapse into one bullet
  naming that component once.
- Protocol changes stay, briefly, with the protocol version they bring
  (e.g. "Protocol 1.3: playlist editing, `feedback.unlike`/`undislike`") —
  details belong in `docs/protocol-changelog.md`.
- Keep bug fixes, new features, new backends/services, new
  packaging/install/release options (e.g. the AppImage) — these are what
  users read the changelog for. Still aim to leave only what matters most:
  fold minor entries (small visual polish) into a closely related bullet.

Preserve the formatting: one heading per release (`# <x.y.z> (<date>)`,
`# X.X.X (<date>)` while unreleased), one `- ` bullet per entry, in English.
Do not touch already-released sections.

Order entries in three groups: new features, then changes to existing
behavior (including docs/process changes), then bug fixes. Within a group,
keep the original newest-first order.

## Making a release

1. Pull fresh tags: `git fetch --tags --force origin` (a tag may have been
   moved or created on GitHub).
2. Run `./update-changelog-from-git.sh`.
3. Tidy up the new section (see above).
4. Pick the version: only bug fixes → patch (`0.1.0` → `0.1.1`); anything
   new → minor (`0.1.0` → `0.2.0`). Replace `X.X.X` in the heading with it.
5. Commit the changelog as `Release <x.y.z>`.
6. Create an annotated tag on that commit whose message is the version
   followed by that release's changelog bullets, and push both:

   ```bash
   git tag -a <x.y.z> -F <message-file>
   git push origin HEAD <x.y.z>
   ```
