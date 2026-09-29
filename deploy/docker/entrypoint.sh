#!/bin/sh
set -eu
mkdir -p /data
cd /app
exec /app/knowledgeos
