#!/bin/bash

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This is inspired by
# https://searchfox.org/mozilla-central/rev/2cd2d511c0d94a34fb7fa3b746f54170ee759e35/taskcluster/scripts/misc/android-gradle-dependencies/after.sh.
# gradle-plugins was removed because it's not used in this project.

set -x -e -v

echo "running as $(id)"

: WORKSPACE "${WORKSPACE:=/builds/worker/workspace}"
ARTIFACTS_TARGET_DIR='/builds/worker/artifacts'


function _package_artifacts_downloaded_by_nexus() {
    pushd "$WORKSPACE"
    mkdir -p external-gradle-dependencies "$ARTIFACTS_TARGET_DIR"

    cp -R "${NEXUS_WORK}/storage/google" external-gradle-dependencies
    cp -R "${NEXUS_WORK}/storage/central" external-gradle-dependencies

    tar cf - external-gradle-dependencies | xz > "$ARTIFACTS_TARGET_DIR/external-gradle-dependencies.tar.xz"

    popd
}


_package_artifacts_downloaded_by_nexus
