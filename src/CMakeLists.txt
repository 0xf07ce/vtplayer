add_executable(vtamp
    main.cpp

    # app
    app/Application.h
    app/Application.cpp

    # audio
    audio/AudioEngine.h
    audio/AudioEngine.cpp
    audio/TrackInfo.h
    audio/RingBuffer.h
    audio/Fft.h

    # view
    view/Theme.h
    view/HeaderBar.h
    view/HeaderBar.cpp
    view/FileBrowser.h
    view/FileBrowser.cpp
    view/PlaylistView.h
    view/PlaylistView.cpp
    view/TransportBar.h
    view/TransportBar.cpp
    view/VisualizerView.h
    view/VisualizerView.cpp

    # visualizer
    visualizer/Visualizer.h
    visualizer/AudioSpectrum.h
    visualizer/AudioSpectrum.cpp

    # config
    config/Config.h
    config/Config.cpp
)

target_include_directories(vtamp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vtamp ventty vtampDep)
