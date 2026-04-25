#!/usr/bin/env bash

sudo apt update
sudo apt install -y nodejs npm git

mkdir gemini-cli
cd gemini-cli

npm init -y
npm install @google/generative-ai
