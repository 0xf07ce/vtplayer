add_executable(vtplayer
    main.cpp

    # app
    app/Application.h
    app/Application.cpp

    # audio
    audio/AudioEngine.h
    audio/AudioEngine.cpp
    audio/Decoder.h
    audio/Decoder.cpp
    audio/ReplayGain.h
    audio/ReplayGain.cpp
    audio/StreamSource.h
    audio/StreamSource.cpp
    audio/TrackInfo.h
    audio/RingBuffer.h
    audio/Fft.h
    audio/ISampleSource.h
    audio/PluginSource.h
    audio/PluginSource.cpp

    # plugin host
    plugin/PluginHost.h
    plugin/PluginHost.cpp
    plugin/DecoderRegistry.h
    plugin/DecoderRegistry.cpp

    # view
    view/Theme.h
    view/HeaderBar.h
    view/HeaderBar.cpp
    view/FileBrowser.h
    view/FileBrowser.cpp
    view/LibraryView.h
    view/LibraryView.cpp
    view/LibrarySearchDialog.h
    view/LibrarySearchDialog.cpp
    view/PlayQueueView.h
    view/PlayQueueView.cpp
    view/TagEditDialog.h
    view/TagEditDialog.cpp
    view/TransportBar.h
    view/TransportBar.cpp
    view/VisualizerView.h
    view/VisualizerView.cpp
    view/ContextMenu.h
    view/ContextMenu.cpp
    view/PlaylistsView.h
    view/PlaylistsView.cpp
    view/TextInputDialog.h
    view/TextInputDialog.cpp
    view/ConfirmDialog.h
    view/ConfirmDialog.cpp

    # visualizer
    visualizer/Visualizer.h
    visualizer/AudioSpectrum.h
    visualizer/AudioSpectrum.cpp
    visualizer/Oscilloscope.h
    visualizer/Oscilloscope.cpp
    visualizer/MatrixRain.h
    visualizer/MatrixRain.cpp
    visualizer/DebugBars.h
    visualizer/DebugBars.cpp
    visualizer/TagInfoView.h
    visualizer/TagInfoView.cpp
    visualizer/VinylVis.h
    visualizer/VinylVis.cpp

    # config
    config/Config.h
    config/Config.cpp

    # library
    library/MediaLibrary.h
    library/MediaLibrary.cpp
    library/LibraryRepository.h
    library/LibraryRepository.cpp
    library/LibraryScanner.h
    library/LibraryScanner.cpp

    # playqueue
    playqueue/PlayQueue.h
    playqueue/PlayQueue.cpp
    playqueue/PlayQueueCache.h
    playqueue/PlayQueueCache.cpp

    # util
    util/M3uReader.h
    util/M3uReader.cpp
    util/PlsReader.h
    util/PlsReader.cpp
    util/PlaylistStore.h
    util/PlaylistStore.cpp
    util/UnicodeNormalize.h
    util/UnicodeNormalize.cpp
    util/TagLibSilencer.h
    util/TagLibSilencer.cpp
    util/TagWriter.h
    util/TagWriter.cpp
)

target_include_directories(vtplayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include   # public plugin ABI header (vtplayer/plugin.h)
)
# ${CMAKE_DL_LIBS} is libdl on Linux, empty on macOS (dlopen lives in libc).
target_link_libraries(vtplayer ventty vtplayerDep ${CMAKE_DL_LIBS})
target_compile_definitions(vtplayer PRIVATE VTPLAYER_VERSION="${PROJECT_VERSION}")
