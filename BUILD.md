## Building YTsaurus from sources

#### Build Requirements
 We have tested YTsaurus C++ SDK builds using 20.04. Other Linux distributions are likely to work, but additional effort may be needed. Only x86_64 Linux is currently supported.

 Below is a list of packages that need to be installed before building YTsaurus. 'How to Build' section contains step by step instructions to obtain these packages.

 - cmake 3.22+
 - clang-16
 - lld-16
 - lldb-16
 - conan 2.4.1
 - git 2.20+
 - python 3.8+
 - pip3
 - ninja 1.10+
 - m4
 - protoc
 - unzip

#### How to Build

 1. Add repositories for dependencies.

    Note: the following repositories are required for the most of Linux distributions. You may skip this step if your GNU/Linux distribution has all required packages in their default repository.
    For more information please read your distribution documentation and https://apt.llvm.org as well as https://apt.kitware.com/
    ```
    curl -s https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add
    curl -s https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
    echo "deb http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-16 main" | sudo tee /etc/apt/sources.list.d/llvm.list >/dev/null
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
    sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test

    sudo apt-get update
    ```

 1. Install dependencies.

    ```
    sudo apt-get install -y python3-pip ninja-build m4 cmake unzip
    sudo apt-get install -y clang-16 lld-16 libc++1-16 libc++-16-dev libc++abi-16-dev g++-11
    sudo python3 -m pip install PyYAML==6.0.1 conan==2.4.1 dacite
    ```
 1. Install protoc.

    ```
    curl -sL -o protoc.zip https://github.com/protocolbuffers/protobuf/releases/download/v3.20.1/protoc-3.20.1-linux-x86_64.zip
    sudo unzip protoc.zip -d /usr/local
    rm protoc.zip
    ```

 1. Clone the YTsaurus repository.
    ```
    git clone https://github.com/ytsaurus/ytsaurus-cpp-sdk.git
    ```

 1. Build YTsaurus.

    Run cmake to generate build configuration:

    ```
    cd build
    cmake \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=../ytsaurus-cpp-sdk/clang.toolchain \
        -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=../ytsaurus-cpp-sdk/cmake/conan_provider.cmake \
        -DREQUIRED_LLVM_TOOLING_VERSION=16 \
        -DCMAKE_CXX_FLAGS_INIT="-stdlib=libc++" \
        ../ytsaurus-cpp-sdk
    ```

    To build just run:
    ```
    ninja
    ```

    If you need to build concrete target you can run:
    ```
    ninja <target>
    ```
