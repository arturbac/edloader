# Changelog

## Unreleased

- Built with edplugin_common 1.0.0, the library the plugins share: a clone in the plugin's directory on the commit
  `edplugin_common.commit` pins. Without the clone the build stops with the commands that make it; another commit or
  changes in it are a warning, and an error in a release build. The log says the library's commit in a line of its
  own after the version (`edplugin_common <hash>`).
- The d3d11 exports are made at configure time by edplugin_common (`edplugin_common_proxy_exports`) instead of the
  copy of EDVR's generator: the build no longer needs Python 3. The dll's exports are unchanged.

## 1.3.1 and earlier

The notes of each release are with its tag on GitHub.
