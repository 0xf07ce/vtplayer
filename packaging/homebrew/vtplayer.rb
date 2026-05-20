class Vtplayer < Formula
  desc "Terminal-based music player for MP3, OGG, FLAC, and WAV"
  homepage "https://github.com/0xf07ce/vtplayer"
  url "https://github.com/0xf07ce/vtplayer/archive/refs/tags/v0.4.0.tar.gz"
  sha256 "06fae90e788d355774be7b9bb28ff9d4b9cb9b82f777010876f80500a27945d5"
  license "LGPL-2.1-or-later"

  # The `bottle do` block is written automatically by the release workflow
  # (`brew bottle --merge --write`); do not edit it by hand.
  bottle do
    root_url "https://github.com/0xf07ce/vtplayer/releases/download/v0.4.0"
    sha256 cellar: :any_skip_relocation, arm64_sequoia: "0a09f22c6b17b981e2ac3a2df957b0d5ae640c9a552d64e48e6595c3d75005b3"
    sha256 cellar: :any_skip_relocation, arm64_tahoe:   "bb5d2a41b50f146001dfb5e1023f09bf34b28756a4792a88507e9a90482fc8ea"
  end

  depends_on "cmake" => :build
  depends_on "pkg-config" => :build

  # Previously git-cloned by CMake FetchContent and built from source on every
  # bottle. Now taken from prebuilt Homebrew bottles via
  # -DVTPLAYER_USE_SYSTEM_DEPS=ON (find_package). This removes the TagLib
  # source build and per-dependency resource sha256 churn.
  depends_on "cxxopts"
  depends_on "sqlite"
  depends_on "taglib"

  # ffmpeg is required at both build (libav headers + pkg-config) and runtime
  # (linked shared libraries). vtplayer delegates *all* audio decoding —
  # local files and internet-radio streams — to libavformat/libavcodec/
  # libswresample, so the formerly optional runtime `ffmpeg` binary dependency
  # has been replaced with a mandatory library-level link.
  depends_on "ffmpeg"

  # ventty has no upstream install()/export() rules, so it cannot be a
  # find_package dependency. It is small and tagged infrequently, so it stays
  # staged as a resource and fed to CMake via FETCHCONTENT_SOURCE_DIR_VENTTY
  # (no network during the build). This resource's sha256 is bumped
  # automatically by the release workflow's `prepare` job.
  resource "ventty" do
    url "https://github.com/0xf07ce/ventty/archive/refs/tags/v0.2.0.tar.gz"
    sha256 "960fa4f8305b3b3bed3f7ae4bb74081c48ebe3448df1eb842462fadd6666a782"
  end

  def install
    ventty_src = buildpath/"_deps/ventty"
    resource("ventty").stage ventty_src

    system "cmake", "-S", ".", "-B", "build", *std_cmake_args,
           "-DVTPLAYER_USE_SYSTEM_DEPS=ON",
           "-DFETCHCONTENT_SOURCE_DIR_VENTTY=#{ventty_src}"
    system "cmake", "--build", "build"
    bin.install "build/vtplayer"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/vtplayer --version")
  end
end
