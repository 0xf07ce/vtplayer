# packaging/homebrew

`vtplayer.rb` here is a **reference copy** of the formula that lives in the
separate tap repo `0xf07ce/homebrew-tap` at `Formula/vtplayer.rb`. It is kept
in this repo so formula changes are reviewable alongside the code/CMake
changes they depend on. The authoritative copy is the one in the tap.

## One-time setup to enable automated bottling

1. **Sync the formula to the tap.** Copy `vtplayer.rb` into
   `0xf07ce/homebrew-tap` as `Formula/vtplayer.rb` and commit it once.
   After this, the release workflow keeps it up to date automatically.

2. **Create the `TAP_PUSH_TOKEN` secret** on the `0xf07ce/vtplayer` repo
   (Settings → Secrets and variables → Actions). It must be a fine-grained
   Personal Access Token with **Contents: Read and write** on
   `0xf07ce/homebrew-tap`. The default `GITHUB_TOKEN` cannot push to a
   different repository, so this token is required for the automation.

## What the release workflow automates

On `git push origin vX.Y.Z` (or `workflow_dispatch`), `release.yml`:

1. **prepare** — ensures the GitHub Release exists, then bumps the tap
   formula's `url`, source `sha256`, and the `ventty` resource `sha256` to
   the new tag and commits to `0xf07ce/homebrew-tap`. (No manual version
   bump; the old "verify version matches" gate is gone.)
2. **bottle** (macos-15, macos-26) — `brew install --build-bottle` then
   `brew bottle --json`, uploads the bottle tarball to the GitHub Release
   and the `.json` as a workflow artifact.
3. **merge** — runs `brew bottle --merge --write --no-commit` over all
   bottle JSONs to rewrite the `bottle do` block and commits it to the tap.

The `bottle do` block in `vtplayer.rb` is machine-managed — do not edit it
by hand.

## ventty resource sha bump

`prepare` updates the `ventty` resource to the tag in
`deps/CMakeLists.txt` (`FetchContent_Declare(ventty ... GIT_TAG vX.Y.Z)`).
When bumping ventty, change the `GIT_TAG` there and the workflow keeps the
formula resource in sync.
