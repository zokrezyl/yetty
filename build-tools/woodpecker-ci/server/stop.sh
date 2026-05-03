#!/usr/bin/env bash


export WOODPECKER_HOST=192.168.1.10
export WOODPECKER_GITHUB_CLIENT=fake
export WOODPECKER_GITHUB_SECRET=fake
export WOODPECKER_AGENT_SECRET=fake

docker-compose down
