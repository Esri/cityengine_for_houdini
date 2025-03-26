#!/bin/sh

# modifies uid and gid for the default user and fixes ownership of the working dir
init() {
    if [ -n "$DEFAULT_GID" ]; then
        echo "[INFO] Setting gid of 'default' group to ${DEFAULT_GID}."
        groupmod -g "$DEFAULT_GID" default || { echo "[ERROR] Failed to set gid of 'default' group to ${DEFAULT_GID}!" >&2; exit 1; }
        default_gid="$DEFAULT_GID" # we need this later
    else
        echo "[INFO] Environment variable 'DEFAULT_GID' not set. Won't change gid of group 'default'."
        default_gid="$(id -g default)"
    fi

    if [ -n "$DEFAULT_UID" ]; then
        echo "[INFO] Setting uid of 'default' user to ${DEFAULT_UID}."
        usermod -u "$DEFAULT_UID" -g "$default_gid" default || { echo "[ERROR] Failed to set uid of 'default' user to $DEFAULT_UID!" >&2; exit 1; }
    else
        echo "[INFO] Environment variable 'DEFAULT_UID' not set. Won't change uid of user 'default'."
    fi

    # Finally, fix ownership of WORKDIR (cf. Dockerfile)
    chown -R default:default /tmp/work ||  { echo '[ERROR] Failed to set ownership of workdir /tmp/work!' >&2; exit 1; }
}

{
    set -e

    # Do our part of container initialization...
    init

    # Continue, dropping privileges with 'setpriv' (see su(1), runuser(1), and setpriv(1) for details).
    cmd="$1"; shift
    exec setpriv --reuid=$(id -u default) --regid=$(id -g default) --clear-groups $cmd "$@"

    # we use exec to replace the entrypoint process (PID 1) with our actual command
    # so that the container is shutdown correctly if the command receives a SIGTERM
    # https://stackoverflow.com/questions/32255814/what-purpose-does-using-exec-in-docker-entrypoint-scripts-serve/32261019#32261019
}

