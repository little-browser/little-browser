#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Bring up the per-release EC2 Mac builder. CI only: runs on the
# lb-linux-protected runner, whose instance role may allocate and use the host.
#
#   desktop/scripts/macos-host.sh up
#
# 1. reuse the dedicated host tagged lb:role=macos-builder, or allocate one;
# 2. mark it busy (lb:busy-until), so the host reaper leaves it alone;
# 3. launch the builder launch template on it, unless an instance is already running;
# 4. attach the persistent data volume (build tree, downloads, sccache), if there is one;
#    LB_MACOS_DATA_VOLUME=none means a cold build on the instance's own disk.
# The instance then starts the lb-macos runner, which picks up the build jobs.
# The reaper releases the host once it is past its 24 h minimum and not busy.
#
# Settings come from CI/CD variables written by the infrastructure repository:
# LB_MACOS_AZ, LB_MACOS_INSTANCE_TYPE, LB_MACOS_LAUNCH_TEMPLATE, LB_MACOS_DATA_VOLUME,
# LB_MACOS_BUSY_HOURS, LB_AWS_REGION.
set -euo pipefail

: "${LB_MACOS_AZ:?}" "${LB_MACOS_INSTANCE_TYPE:?}" "${LB_MACOS_LAUNCH_TEMPLATE:?}" "${LB_MACOS_DATA_VOLUME:?}"
REGION="${LB_AWS_REGION:-ap-southeast-2}"
BUSY_HOURS="${LB_MACOS_BUSY_HOURS:-8}"

die() { echo "error: $*" >&2; exit 1; }
ec2() { aws --region "$REGION" ec2 "$@"; }

# wait_for <seconds> <description> <command...>: poll every 30 s until the command succeeds.
wait_for() {
  local limit=$1 what=$2 waited=0
  shift 2
  until "$@"; do
    [ "$waited" -lt "$limit" ] || die "timed out waiting for $what"
    sleep 30; waited=$((waited + 30))
  done
}

up() {
  [ "$LB_MACOS_LAUNCH_TEMPLATE" != none ] ||
    die "no macOS builder image yet: set macos_ami_id in the infrastructure repository"

  local host
  host="$(ec2 describe-hosts \
    --filter Name=tag:lb:role,Values=macos-builder Name=state,Values=available,pending,under-assessment \
    --query 'Hosts[0].HostId' --output text)"
  if [ -z "$host" ] || [ "$host" = None ]; then
    echo "allocating a $LB_MACOS_INSTANCE_TYPE host in $LB_MACOS_AZ (24 h minimum)"
    host="$(ec2 allocate-hosts --instance-type "$LB_MACOS_INSTANCE_TYPE" \
      --availability-zone "$LB_MACOS_AZ" --quantity 1 \
      --tag-specifications 'ResourceType=dedicated-host,Tags=[{Key=lb:role,Value=macos-builder},{Key=Name,Value=little-browser-macos}]' \
      --query 'HostIds[0]' --output text)"
  else
    echo "reusing host $host"
  fi

  # Busy first, so the reaper can't take the host away while this pipeline uses it.
  ec2 create-tags --resources "$host" \
    --tags "Key=lb:busy-until,Value=$(date -u -d "+$BUSY_HOURS hours" +%Y-%m-%dT%H:%M:%SZ)"

  local instance
  instance="$(ec2 describe-instances \
    --filters "Name=host-id,Values=$host" Name=instance-state-name,Values=pending,running \
    --query 'Reservations[0].Instances[0].InstanceId' --output text)"
  if [ -z "$instance" ] || [ "$instance" = None ]; then
    # A host scrubs itself for a while after its last instance terminated.
    host_available() {
      [ "$(ec2 describe-hosts --host-ids "$host" --query 'Hosts[0].State' --output text)" = available ]
    }
    wait_for 10800 "host $host to become available" host_available
    instance="$(ec2 run-instances \
      --launch-template "LaunchTemplateId=$LB_MACOS_LAUNCH_TEMPLATE" \
      --placement "HostId=$host,Tenancy=host" \
      --query 'Instances[0].InstanceId' --output text)"
    echo "launched $instance on $host"
  else
    echo "instance $instance already on $host"
  fi
  ec2 wait instance-running --instance-ids "$instance"

  if [ "$LB_MACOS_DATA_VOLUME" = none ]; then
    echo "no persistent data volume: cold build on the instance's own disk"
    echo "macOS builder: host $host, instance $instance; the lb-macos runner comes online once it has booted"
    return 0
  fi

  local attached_to
  attached_to="$(ec2 describe-volumes --volume-ids "$LB_MACOS_DATA_VOLUME" \
    --query 'Volumes[0].Attachments[0].InstanceId' --output text)"
  if [ "$attached_to" != "$instance" ]; then
    volume_available() {
      [ "$(ec2 describe-volumes --volume-ids "$LB_MACOS_DATA_VOLUME" \
        --query 'Volumes[0].State' --output text)" = available ]
    }
    wait_for 900 "data volume $LB_MACOS_DATA_VOLUME to detach" volume_available
    ec2 attach-volume --volume-id "$LB_MACOS_DATA_VOLUME" --instance-id "$instance" --device /dev/sdf >/dev/null
    echo "attached $LB_MACOS_DATA_VOLUME to $instance"
  fi
  echo "macOS builder: host $host, instance $instance; the lb-macos runner comes online once it has booted"
}

case "${1:-}" in
  up) up ;;
  *) echo "usage: macos-host.sh up" >&2; exit 1 ;;
esac
