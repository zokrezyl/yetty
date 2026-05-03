#!/usr/bin/env bash


export WOODPECKER_HOST=http://192.168.1.10:8000
export WOODPECKER_GITHUB_CLIENT=Ov23lit3VyHJpTSx2gxr
export WOODPECKER_SERVER=192.168.1.10:9000
#export WOODPECKER_GITHUB_SECRET=fake
#export WOODPECKER_AGENT_SECRET=fake

docker-compose up -d
