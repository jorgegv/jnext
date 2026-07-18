# Contributing to JNEXT

Contributions are welcome. Bugfixes and features are accepted through pull
requests, reviewed strictly against a fixed protocol — a PR that does not
comply is not merged.

In short, every PR must:

- ship **discriminative tests** for its change (they fail without the change and
  pass with it);
- **not modify existing tests** (without owner approval);
- use only **license-clean fixtures**;
- match the project's **code style**;
- **not add dependencies** (without owner approval).

A **bugfix** PR also needs a full bug description (or a linked bug issue); a
**feature** PR also needs an explicit use case and a design document under
`doc/`.

Full rules: **[doc/PULL-REQUEST-PROTOCOL.md](doc/PULL-REQUEST-PROTOCOL.md)**.
