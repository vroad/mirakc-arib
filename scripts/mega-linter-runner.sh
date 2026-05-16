#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

# mirakc-arib
# Copyright (C) 2019 masnagam
#
# This program is free software; you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
# the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program; if
# not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

set -eu

# MegaLinter Docker image for the c_cpp flavor, digest-pinned so local runs
# match .github/workflows/mega-linter.yml.
# The digest and the version comment below are kept up to date by Renovate (.github/renovate.json).
#
# editorconfig-checker-disable-next-line
readonly IMAGE='ghcr.io/oxsecurity/megalinter-c_cpp@sha256:108754f909ab6c9409b1114a7e24b5ed137e1ac3e51a8b574c045692ca70fbf9' # v8.8.0

basedir=$(cd "$(dirname "$0")"; pwd)
projdir=$(cd "${basedir}/.."; pwd)

cd "${projdir}"
pnpm install --frozen-lockfile --prefer-offline
exec pnpm exec mega-linter-runner --image "${IMAGE}" "$@"
