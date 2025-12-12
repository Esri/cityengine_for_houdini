ARG IMAGE_OS_BASE=almalinux
ARG IMAGE_OS_VERSION=8
FROM ${IMAGE_OS_BASE}:${IMAGE_OS_VERSION}

# Setting up base OS with GCC 11 and Python 3.12
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

# Adding 'default' user to do the work
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

# Putting Python user base outside of the workdir so we can mount it from the outside
ENV PYTHONUSERBASE=/opt/pyuser
RUN set -e \
    && mkdir -p ${PYTHONUSERBASE} \
    && chown -R default:default ${PYTHONUSERBASE} \
    && chmod -R 755 ${PYTHONUSERBASE}
ENV PATH=${PYTHONUSERBASE}/bin:${PATH}

# Putting conan cache outside of the workdir due to mounting and slow chmod/chown in docker volumes on RHEL8 kernels
# (the cache can get big with lots of files). Using RO mode to support consuming the cache with different UID/GID.
ENV CONAN_CACHE_NO_LOCKS=1
ENV CONAN_READ_ONLY_CACHE=1
ENV CONAN_USER_HOME=/opt/conan
RUN set -e \
    && mkdir -p ${CONAN_USER_HOME} \
    && chown -R default:default ${CONAN_USER_HOME} \
    && chmod -R 755 ${CONAN_USER_HOME}

# Setting up the Python environment
USER default
RUN python3.12 -m pip install --user --no-cache-dir --upgrade pip wheel "conan<2.0"

# Pre-installing the Conan cache
ARG CONAN_RECIPES=/tmp/conan-recipes

ARG CESDK_VERSION=3.3.11173
RUN --mount=type=bind,source=./conan,target=${CONAN_RECIPES},ro \
    conan create -pr ${CONAN_RECIPES}/profiles/linux-gcc112 ${CONAN_RECIPES}/cesdk \
    cesdk/${CESDK_VERSION}@esri-rd-zurich/stable

ARG HOUDINI_VERSION=21.0.559
RUN --mount=type=bind,source=./conan,target=${CONAN_RECIPES},ro \
    --mount=type=bind,from=houdini,source=.,target=/tmp/Houdini${HOUDINI_VERSION},ro \
    conan create -pr ${CONAN_RECIPES}/profiles/linux-gcc112 -e HOUDINI_INSTALL=/tmp/Houdini${HOUDINI_VERSION} \
    ${CONAN_RECIPES}/houdini houdini/${HOUDINI_VERSION}@sidefx/stable

RUN conan download catch2/2.13.7@

# Setting up the container entrypoint
USER root
RUN echo "unset BASH_ENV PROMPT_COMMAND ENV && source scl_source enable gcc-toolset-11" >> /tmp/scl_enable
ENV BASH_ENV=/tmp/scl_enable ENV=/tmp/scl_enable PROMPT_COMMAND=". /tmp/scl_enable"
ENTRYPOINT ["container-entrypoint.sh"]
