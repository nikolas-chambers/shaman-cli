# Homebrew formula (builds from the release source). Publish it in a tap repo named homebrew-tap:
#   brew install nikolas-chambers/tap/shaman
# On each release: set url to the new tag's tarball and sha256 to `shasum -a 256` of it.
class Shaman < Formula
  desc "Coding agent for your terminal: your link between worlds"
  homepage "https://nikolas-chambers.github.io/shaman-cli/"
  url "https://github.com/nikolas-chambers/shaman-cli/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "REPLACE_WITH_SHA256_OF_THE_TARBALL"
  license "MIT"
  head "https://github.com/nikolas-chambers/shaman-cli.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "nlohmann-json" => :build
  depends_on "curl"
  depends_on "openssl@3"
  depends_on "llvm" => :build if DevelopmentTools.clang_build_version < 1600  # C++23 std::expected

  def install
    args = %W[-DCMAKE_BUILD_TYPE=Release -DSHAMAN_BUILD_TESTS=OFF -DOPENSSL_ROOT_DIR=#{Formula["openssl@3"].opt_prefix}]
    system "cmake", "-S", ".", "-B", "build", "-G", "Ninja", *args, *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/shaman"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/shaman --version")
  end
end
