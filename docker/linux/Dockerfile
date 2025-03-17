ARG IMAGE_OS_BASE=almalinux
ARG IMAGE_OS_VERSION=8
FROM ${IMAGE_OS_BASE}:${IMAGE_OS_VERSION}

RUN set -e \
    && rpm --import https://repo.almalinux.org/almalinux/RPM-GPG-KEY-AlmaLinux \
    && dnf -y update \
    && dnf install -y dnf-plugins-core https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm \
    && dnf config-manager --set-enabled powertools \
    && INSTALL_PKGS="git unzip bzip2 gcc-toolset-11 gcc-toolset-11-gcc gcc-toolset-11-gcc-c++ gcc-toolset-11-gdb redhat-lsb cmake make ninja-build python3.12 python3.12-pip" \
    && dnf install -y --setopt=tsflags=nodocs $INSTALL_PKGS \
    && rpm -V $INSTALL_PKGS \
    && dnf clean all \
    && rm -rf /var/cache/dnf

ENV HOME=/tmp/work
COPY docker/linux/docker-entrypoint.sh /usr/bin/container-entrypoint.sh
RUN set -e \
    && mkdir -p ${HOME} \
    && groupadd -r default -f -g 1001 \
    && useradd -r default -g default -d ${HOME} -s /sbin/nologin -c "Default Application User" \
    && chown -R default:default ${HOME} \
    && chmod -R 755 ${HOME} \
    && chown default:default /usr/bin/container-entrypoint.sh \
    && chmod 555 /usr/bin/container-entrypoint.sh
WORKDIR ${HOME}

# .local/bin will be used by pip install --user
ENV PATH=${HOME}/.local/bin:${PATH}
USER default
RUN python3.12 -m pip install --user --no-cache-dir --upgrade pip wheel "conan<2.0" \
    && conan remote add zrh-conan http://zrh-conan.esri.com:8081/artifactory/api/conan/conan-local \
    && conan remote list && cat .conan/remotes.json
USER root

RUN echo "unset BASH_ENV PROMPT_COMMAND ENV && source scl_source enable gcc-toolset-11" >> /tmp/scl_enable
ENV BASH_ENV=/tmp/scl_enable ENV=/tmp/scl_enable PROMPT_COMMAND=". /tmp/scl_enable"

ENTRYPOINT ["container-entrypoint.sh"]