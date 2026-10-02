---
title: app.json
---

# app.json

:::warning Stub
Not written yet.
:::

Basalt reads its configuration from `app.json`, under a `basalt` key, beside
whatever Expo already keeps there.

```json
{
  "basalt": {
    "desktops": ["macos", "linux"],
    "identifier": "com.example.app",
    "scheme": "example"
  }
}
```

| key | what it does |
| --- | --- |
| `desktops` | which hosts `init` installs and the build configures |
| `identifier` | the bundle identifier a packaged app gets |
| `scheme` | the URL scheme the app registers |
