#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fetch M-VAVE's FM1 Open SDK (github.com/m-vave/fm1_open) at the commit Ravel is built and
# checked against, into build/fm1_open (or DEST). Its binary library, boot/update resources and
# board code carry their own licences: see THIRD_PARTY.md and LICENSE.md in the checkout.
set -e
COMMIT=b178ac43c25f010c60a878056fb5331fe693fd62
DEST="${1:-$(dirname "$0")/../build/fm1_open}"
if [ ! -d "$DEST/.git" ]; then
    git clone --quiet https://github.com/m-vave/fm1_open.git "$DEST"
fi
git -C "$DEST" fetch --quiet origin
git -C "$DEST" -c advice.detachedHead=false checkout --quiet "$COMMIT"
echo "FM1 Open SDK: $DEST at $COMMIT"
