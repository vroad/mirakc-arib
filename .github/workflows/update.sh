#!/bin/sh -eu

BASEDIR=$(cd $(dirname $0); pwd)
PROJDIR=$(cd $BASEDIR/../..; pwd)

if [ "$(uname)" != Linux ] || id -nG | grep -q docker; then
  DOCKER='docker'
else
  DOCKER='sudo docker'
fi

HEADER=$(cat <<EOF
# DO NOT EDIT THIS FILE BY HAND.
#
# This file was generated by .github/workflows/update.sh automagically.
EOF
)

cat <<EOF >$PROJDIR/.github/workflows/ci.yml
$HEADER
$($DOCKER run --rm -v $BASEDIR/templates:/workdir \
    docker.io/mikefarah/yq ea '. as $item ireduce ({}; . *d $item)' \
    ci.workflow.yml \
    linux-build.job.yml \
    arm-linux-build.job.yml \
    coverage.job.yml
)
EOF

cat <<EOF >$PROJDIR/.github/workflows/pull-request.yml
$HEADER
$($DOCKER run --rm -v $BASEDIR/templates:/workdir \
    docker.io/mikefarah/yq ea '. as $item ireduce ({}; . *d $item)' \
    pull-request.workflow.yml \
    linux-build.job.yml \
    arm-linux-build.job.yml \
    coverage.job.yml
)
EOF
