<!--
  Gatekeeper's behaviour changes between major macOS releases. This page used
  to say "Control-click the app and choose Open", which was correct when it was
  written and silently stopped covering this case on macOS 15 — leaving the one
  instruction we gave as the one that fails. Re-check every route below on real
  macOS hardware whenever a new major release ships, and fix the page rather
  than adding a second, still-untested alternative.
-->
# macOS

Download `jnext-*-Darwin.dmg`, open it, and drag **jnext** to Applications.

The first time you open it, macOS refuses to launch it and says something like
*"jnext" cannot be opened because Apple cannot check it for malicious
software*. That is expected, and the next section explains why. Allowing it
takes one of the three routes below; you only do it once.

## Why the warning appears

JNEXT is not notarised, and that is a deliberate choice. Notarisation is
Apple's approval step for software distributed outside the App Store, and it
requires a paid Apple Developer Program membership renewed every year, plus a
signing certificate held by the project and submitted with each release. This
is a hobby project, and paying for that would not make the emulator any better.

So macOS has nothing from Apple to check the download against, and tells you
so. The warning reports the *absence of an approval* — it does not mean
anything was found wrong with the app.

If you would like to satisfy yourself that the copy you downloaded is intact,
open a terminal and run:

```sh
codesign --verify --deep --strict /Applications/jnext.app
```

It prints nothing if every file in the bundle still matches the signature
applied when the app was built, which means nothing has been altered or added
since. It cannot tell you *who* built it — no unnotarised app can — so if that
is the question that matters to you, route 3 answers it properly.

## Route 1 — allow it in System Settings

This is the route that works on current macOS.

1. Double-click **jnext** in Applications. macOS refuses to open it; dismiss
   the dialog. This step is not optional — the button in step 3 only appears
   after a blocked attempt.
2. Open the Apple menu, choose **System Settings**, select **Privacy &
   Security** in the sidebar, and scroll down to the **Security** section.
3. A line now reads *"jnext" was blocked to protect your Mac*, with an **Open
   Anyway** button next to it. Click it and confirm with Touch ID or your
   login password.
4. Open jnext again. A last dialog asks once more; choose **Open Anyway**.

From then on it opens by double-click like any other app.

If you remember the older Control-click → **Open** trick, it no longer covers
this case on macOS 15 and later. Use the steps above instead.

## Route 2 — clear the quarantine flag from a terminal

```sh
xattr -dr com.apple.quarantine /Applications/jnext.app
```

When your browser downloads a file, macOS tags it with an extended attribute
named `com.apple.quarantine` — the "came from the internet" marker that
triggers the check on first launch. Dragging the app out of the `.dmg` carries
that tag onto your copy. The command removes the attribute from the app and
everything inside it (`-d` deletes an attribute, `-r` recurses into the
bundle), after which it opens normally.

This is you deciding to trust this particular download, which is why it names
one path. Do not apply it to a whole folder, and do not turn Gatekeeper off
system-wide to avoid the prompt: both of these routes cover one app and leave
everything else on your Mac checked exactly as before.

## Route 3 — build it yourself

The quarantine flag comes from downloading, not from running, so an app you
build on your own machine never has one and there is no Gatekeeper step at all.
See [Building from source](06-building-from-source.md).
