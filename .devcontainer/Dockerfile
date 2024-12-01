FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="3.22.2"
ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="none"

# Optionally install the cmake for vcpkg
COPY ./reinstall-cmake.sh /tmp/

# RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
#         chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
#     fi \
#     && rm -f /tmp/reinstall-cmake.sh

# [Optional] Uncomment this section to install additional vcpkg ports.
# RUN su vscode -c "${VCPKG_ROOT}/vcpkg install <your-port-name-here>"

# [Optional] Uncomment this section to install additional packages.
# RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
#     && apt-get -y install --no-install-recommends <your-package-list-here>

# Install gcc 14
RUN apt-get update && apt-get install add-apt-repository \
    && add-apt-repository "deb http://archive.ubuntu.com/ubuntu/ noble main universe" \
    && apt-get update && apt-get -y install gcc-14 g++-14