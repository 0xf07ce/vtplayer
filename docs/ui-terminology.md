# UI Terminology

Use these names when discussing vtplayer's terminal UI.

## Top-level Areas

- **Screen**: a full application state. vtplayer has the **Browser screen**,
  **Visualizer screen**, and **Help screen**.
- **Panel**: a layout region inside the Browser screen. Prefer this over
  "window" because vtplayer runs in one terminal window.
- **View**: the widget or content surface occupying a panel.
- **Mode**: the selected content variant of the source panel.

## Browser Screen

The **Browser screen** is the main two-panel screen:

- **Source panel**: the left panel. It is the navigation/source side where the
  user chooses tracks from the library, filesystem, or saved playlists. In code,
  this is selected by `Application::LeftMode` and occupied by `LibraryView`,
  `FileBrowser`, or `PlaylistsView`.
- **Play queue panel**: the right panel. It is occupied by `PlayQueueView` and
  shows the current session queue. Use **play queue** for this, not "playlist".

When the source panel is hidden, the play queue panel spans the full content
width.

## Source Panel Modes

The source panel has five modes:

- **Album mode** or **Album library view**: `LibraryView` grouped as
  Grouping > Album Artist > Album > Track.
- **Artist mode** or **Artist library view**: `LibraryView` grouped as
  Grouping > Artist > Album > Track.
- **Directory mode** or **Directory library view**: `LibraryView` grouped by
  library-root-relative directory paths.
- **Files mode** or **File browser**: `FileBrowser`, the live filesystem view.
- **Playlists mode** or **Playlist browser**: `PlaylistsView`, the saved
  playlist browser.

Use **library view** only when the distinction between Album, Artist, and
Directory modes is not important.

## Playlist Browser

The **Playlist browser** has two subviews:

- **Playlist list view**: the top-level list of saved playlists.
- **Playlist contents view**: the track list inside one opened saved playlist.
  This view can enter **playlist edit mode** for reordering or deleting tracks;
  edits are persisted when the playlist is saved.

Use **saved playlist** for playlists managed by `PlaylistStore`
(`~/.config/vtplayer/playlists/*.m3u8`). This is separate from the play queue.

## Playlist File Terms

Keep these names distinct:

- **Play queue**: the volatile session queue shown in the play queue panel.
- **Saved playlist**: a user-managed `.m3u8` playlist shown in Playlists mode.
- **M3U playlist file**: a `.m3u` or `.m3u8` file opened from the file browser.
- **PLS radio playlist**: a `.pls` file parsed as internet-radio channels and
  indexed into the library under the stream grouping.
