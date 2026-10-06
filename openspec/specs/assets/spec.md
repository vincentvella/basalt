# assets Specification

## Purpose

`require('./picture.png')`: getting a bundled file from the packager onto the
screen.

Three things have to agree. Metro records which assets a build referenced, the
CLI copies them where the host will look, and the runtime resolves an asset id
back to a path. React Native's own CLI does two of the three and does not know
this platform, so both are this project's.

## Requirements

### Requirement: A bundle carries the assets it referenced

The system SHALL copy every asset a build referenced to a location beside the
bundle, laid out where React Native's own resolver will look for it, including
the per-scale naming it expects.

#### Scenario: Bundling copies the assets

- **WHEN** an app that requires an image is bundled
- **THEN** the image is copied beside the bundle, at the path the resolver
  derives for it

### Requirement: A required asset resolves and draws

The system SHALL resolve an asset id from the registry to the copied file, and
SHALL load it through the same image path a network source uses, so a
`require()`d image and a remote one behave the same once loaded.

#### Scenario: A required image appears

- **WHEN** an app renders an `<Image>` with a `require()`d source
- **THEN** the image is decoded and drawn

### Requirement: An asset that cannot be resolved says why

The system SHALL report a missing or unresolvable asset with the path it looked
for, rather than drawing nothing. A blank box is indistinguishable from an image
that decoded to nothing.

#### Scenario: A missing asset is reported

- **WHEN** an asset referenced by the bundle is not beside it
- **THEN** the failure names the path that was looked for

### Requirement: Remote assets are refused rather than silently missing

The system SHALL reject an asset download it does not implement, naming the
limit, rather than resolving with a path that does not exist.

#### Scenario: A network asset download is refused clearly

- **WHEN** an app asks to download an asset served over http
- **THEN** the call rejects saying so
