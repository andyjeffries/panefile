# Homebrew formula for Panefile.
#
# Install from the tap:
#
#     brew install andyjeffries/tap/panefile
#
# Or, before the tap exists, straight from this file:
#
#     brew install --build-from-source packaging/homebrew/panefile.rb
#
# The optional dependencies are listed as hard ones for the same reason the AUR
# package lists them: §3.4 keeps them out of the binary's load-time
# dependencies, so they cost nothing at startup, and a formula that leaves them
# out hands most people a build quietly missing half of Quick Look.
#
# KSyntaxHighlighting is the exception — it is not in Homebrew, so on macOS the
# text renderer takes the plain-text fallback §2 already requires of it.
class Panefile < Formula
  desc "Keyboard-driven, multi-panel file manager"
  homepage "https://panefile.dev"
  url "https://github.com/andyjeffries/panefile/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "0" * 64 # replaced at release time
  license "MIT"
  head "https://github.com/andyjeffries/panefile.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build

  depends_on "libarchive"
  depends_on "poppler-qt6"
  depends_on "qt"
  depends_on :macos

  def install
    system "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON",
                    *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"

    # The install rules put Panefile.app at the prefix — Launch Services needs a
    # bundle in order to offer Panefile as a folder handler — and bin/pf beside
    # it as a wrapper that execs the bundle's executable.
    #
    # A wrapper rather than a symlink, deliberately: macOS works out what an
    # application is from the path its executable was launched by, and a symlink
    # reports its own path rather than the bundle's. The app would run, but with
    # no Info.plist behind it: the grey placeholder icon in the Dock, and "pf"
    # for a name.
  end

  test do
    # --version needs neither a display server nor a compositor, which is the
    # point of it being answered before any QApplication is constructed.
    assert_match "panefile", shell_output("#{bin}/pf --version")
  end
end
