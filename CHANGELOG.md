# Changelog

## Unreleased

- Built with edplugin_common, the library the plugins share: a clone in the plugin's directory on the commit
  `edplugin_common.commit` pins. Without the clone the build stops with the commands that make it; another commit or
  changes in it are a warning, and an error in a release build. The log says the library's commit in a line of its
  own after the version (`edplugin_common <hash>`).

## 1.3.1 and earlier

The notes of each release are with its tag on GitHub.
