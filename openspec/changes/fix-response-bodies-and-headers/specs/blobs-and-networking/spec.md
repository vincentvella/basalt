# blobs-and-networking Specification

## ADDED Requirements

### Requirement: A response body is the bytes the server sent

The system SHALL deliver a response body unchanged to whichever reader asks for
it -- `text()`, `json()`, `blob()` or `arrayBuffer()` -- on every React Native
version it supports.

Where the platform underneath encodes the body for delivery, the system SHALL
decode it; where it does not, the system SHALL take the body as it arrived. It
SHALL NOT assume which, because both behaviours are live across supported
versions and the app does not choose its own.

Where the distinction cannot be made from the delivered bytes alone, the system
SHALL use the server's own byte count when there is one, and otherwise SHALL
decide from what it has already observed of this platform, which does not change
while the process runs.

#### Scenario: A JSON response parses

- **WHEN** an app calls `fetch(...)` and reads the response as JSON
- **THEN** it parses, and is what the server sent

#### Scenario: The platform does not encode

- **WHEN** the React Native underneath delivers bodies unencoded
- **THEN** a text body is still correct, and the app is told once that a binary
  body read as bytes will not be

### Requirement: Response headers are readable

The system SHALL make a response's headers available to every reader React
Native offers: `getResponseHeader`, `getAllResponseHeaders`, and a `Response`'s
`headers`.

A header sent more than once SHALL be joined, and two spellings of one name
SHALL be one header.

#### Scenario: An app reads a content type

- **WHEN** a response carries `Content-Type` and the app asks for it
- **THEN** it is returned, by the XHR and by `fetch`'s `Response` alike

#### Scenario: A Blob from a response carries its type

- **WHEN** a response with a content type is read as a Blob
- **THEN** the Blob's `type` is that content type
