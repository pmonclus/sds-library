# SDS Library - Homebrew Formula
# 
# For local testing:
#   1. First create tarball: ./packaging/build-package.sh tarball
#   2. Then install: brew install --build-from-source ./packaging/homebrew/sds.rb
#
# For distribution, update the url to point to a GitHub release.

class Sds < Formula
  desc "SDS - Synchronized Data Structures for IoT"
  homepage "https://github.com/pmonclus/sds-library"
  version "0.3.1"
  license "MIT"

  # Local tarball for testing (created by build-package.sh)
  url "file://#{ENV['HOME']}/sds-source-#{version}.tar.gz"
  sha256 :no_check  # Disable checksum for local development

  depends_on "cmake" => :build
  depends_on "libpaho-mqtt"
  depends_on "python@3.12"

  def install
    # Build C library with CMake
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_INSTALL_PREFIX=#{prefix}",
           "-DSDS_BUILD_TESTS=OFF",
           "-DSDS_BUILD_EXAMPLES=OFF",
           *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"

    # Install Python package (sds bindings + codegen)
    system Formula["python@3.12"].opt_bin/"python3", "-m", "pip", "install",
           "--prefix=#{prefix}",
           "--no-deps",
           "."

    # Install sds-codegen wrapper script
    bin.install "packaging/common/sds-codegen"

    # Create Arduino package directory and ZIP
    (share/"sds").mkpath
    
    # Create Arduino ZIP from source files
    arduino_dir = buildpath/"arduino_package/SDS"
    arduino_dir.mkpath
    (arduino_dir/"src").mkpath
    (arduino_dir/"examples/BasicDevice").mkpath

    # Copy Arduino files
    cp Dir["include/*.h"].reject { |f| f.include?("sds_types.h") }, arduino_dir/"src/"
    cp "src/sds_core.c", arduino_dir/"src/"
    cp "src/sds_json.c", arduino_dir/"src/"
    cp "platform/esp32/sds_platform_esp32.cpp", arduino_dir/"src/"
    
    # Create library.properties
    (arduino_dir/"library.properties").write <<~EOS
      name=SDS
      version=#{version}
      author=SDS Team
      maintainer=SDS Team
      sentence=Lightweight MQTT state synchronization for IoT
      paragraph=Synchronized Data Structures library for ESP32/ESP8266
      category=Communication
      url=https://github.com/pmonclus/sds-library
      architectures=esp32,esp8266
      depends=PubSubClient
    EOS

    # Create the ZIP
    cd buildpath/"arduino_package" do
      system "zip", "-r", share/"sds/sds-arduino-#{version}.zip", "SDS"
    end

    # Also install codegen tools to share directory
    (share/"sds/tools/codegen").mkpath
    cp_r "codegen/.", share/"sds/tools/codegen/"
    cp "tools/sds_codegen.py", share/"sds/tools/"
  end

  def caveats
    <<~EOS
      SDS Library installed!

      Usage:
        # Generate types from schema
        sds-codegen schema.sds --c --python

        # Arduino library
        The Arduino library ZIP is available at:
          #{share}/sds/sds-arduino-#{version}.zip

        Install in Arduino IDE:
          Sketch → Include Library → Add .ZIP Library

      Python:
        The SDS Python module is installed to:
          #{lib}/python3.12/site-packages/

        You may need to add this to your PYTHONPATH:
          export PYTHONPATH="#{lib}/python3.12/site-packages:$PYTHONPATH"

      C Library:
        Headers: #{include}/sds/
        Library: #{lib}/libsds.a
    EOS
  end

  test do
    # Test sds-codegen
    (testpath/"test.sds").write <<~EOS
      @version = "1.0.0"
      table TestTable {
          @sync_interval = 1000
          config { uint8 value; }
          state { float temp; }
          status { uint8 code; }
      }
    EOS
    
    system "sds-codegen", "test.sds", "--c"
    assert_predicate testpath/"sds_types.h", :exist?
  end
end
