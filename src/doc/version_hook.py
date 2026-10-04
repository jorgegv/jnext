# mkdocs hook shared by mkdocs.yml (user guide) and mkdocs-devguide.yml
# (developer guide): sets config.extra.doc_release, the "This version: vX.Y.Z"
# shown in each guide's header, from version.yaml at render time.
#
# version.yaml is the single source of truth for the version, so neither config
# carries a copy of it and a bump edits neither config. The rendered guides are
# still committed and still show the version (offline reading of a clone), so a
# bump still re-renders them.
#
# No silent default: a missing or malformed version.yaml stops the build. A
# guide rendered with an empty or invented version would pass docs-check on the
# next run with that same wrong value, so failing here is the only place to
# catch it. Same field and X.Y.Z form cmake/GenerateVersion.cmake reads.
import os
import re

from mkdocs.exceptions import PluginError


def on_config(config):
    path = os.path.join(os.path.dirname(config.config_file_path), "version.yaml")
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise PluginError(f"version_hook: cannot read {path}: {e}")
    m = re.search(r"^version: *([0-9]+\.[0-9]+\.[0-9]+) *$", text, re.MULTILINE)
    if not m:
        raise PluginError(f"version_hook: no 'version: X.Y.Z' line in {path}")
    config.extra["doc_release"] = "v" + m.group(1)
    return config
