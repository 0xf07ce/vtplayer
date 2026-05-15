add_executable(vtplayer
    main.cpp

    # app
    app/Application.h
    app/Application.cpp

    # audio
    audio/AudioEngine.h
    audio/AudioEngine.cpp
    audio/ReplayGain.h
    audio/ReplayGain.cpp
    audio/TrackInfo.h
    audio/RingBuffer.h
    audio/Fft.h

    # view
    view/Theme.h
    view/HeaderBar.h
    view/HeaderBar.cpp
    view/FileBrowser.h
    view/FileBrowser.cpp
    view/PlayQueueView.h
    view/PlayQueueView.cpp
    view/TransportBar.h
    view/TransportBar.cpp
    view/VisualizerView.h
    view/VisualizerView.cpp
    view/ContextMenu.h
    view/ContextMenu.cpp

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

    # config
    config/Config.h
    config/Config.cpp

    # library
    library/MediaLibrary.h
    library/MediaLibrary.cpp

    # playqueue
    playqueue/PlayQueue.h
    playqueue/PlayQueue.cpp

    # util
    util/M3uReader.h
    util/M3uReader.cpp
    util/UnicodeNormalize.h
    util/UnicodeNormalize.cpp
)

target_include_directories(vtplayer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vtplayer ventty vtplayerDep)
target_compile_definitions(vtplayer PRIVATE VTPLAYER_VERSION="${PROJECT_VERSION}")
