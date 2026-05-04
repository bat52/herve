#!/bin/bash

SESSION="agent"

tmux new-session -d -s $SESSION

# Rename window
tmux rename-window -t $SESSION "main"

# Create 3-pane layout: left column (ranger), right column split top/bottom (bash, cline)
tmux split-window -h -t $SESSION        # right column
tmux split-window -v -t $SESSION:0.1    # split right column

# Launch tools
tmux send-keys -t $SESSION:0.0 "ranger" C-m        # file browser
tmux send-keys -t $SESSION:0.1 "bash" C-m          # shell
tmux send-keys -t $SESSION:0.2 "cline --tui" C-m   # agent

# Focus on shell by default
tmux select-pane -t $SESSION:0.1

tmux attach -t $SESSION
