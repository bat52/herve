#!/bin/bash

SESSION="$(basename "$PWD")"

if tmux has-session -t "$SESSION" 2>/dev/null; then
    tmux attach -t "$SESSION"
    exit 0
fi

tmux new-session -d -s "$SESSION"
tmux attach -t "$SESSION"
