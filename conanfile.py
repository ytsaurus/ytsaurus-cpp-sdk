from conan import ConanFile


class App(ConanFile):

    settings = "os", "compiler", "build_type", "arch"

    options = {}

    requires = "linux-headers-generic/6.5.9", "protobuf/3.19.4"

    tool_requires = "protobuf/3.19.4", "ragel/6.10", "yasm/1.3.0"
    generators = "cmake_find_package", "cmake_paths"

    def imports(self):
        self.copy(pattern="*yasm*", src="bin", dst="./bin")
        self.copy(pattern="ragel*", src="bin", dst="./bin")
        self.copy(pattern="ytasm*", src="bin", dst="./bin")
        self.copy(pattern="protoc*", src="bin", dst="./bin")

    def layout(self):
        cmake_layout(self)
