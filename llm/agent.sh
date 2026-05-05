#!/bin/bash

SESSION="agent-$(basename "$PWD")-$(hostname -s)"

# If session exists → just attach
if tmux has-session -t $SESSION 2>/dev/null; then
    
    # auto repair
    PANE_COUNT=$(tmux list-panes -t $SESSION | wc -l)
    if [ "$PANE_COUNT" -ne 2 ]; then
        tmux kill-session -t $SESSION
        exec $0
    fi

    # attach to existing session
    tmux attach -t $SESSION
    exit 0
fi

tmux new-session -d -s $SESSION

# Rename window
tmux rename-window -t $SESSION "main"

# Create 2-pane layout: top (cline), bottom (shell)
tmux split-window -v -t $SESSION

# Launch tools
tmux send-keys -t $SESSION:0.0 "cline --tui" C-m   # agent
tmux send-keys -t $SESSION:0.1 "bash" C-m          # shell

# Focus on cline by default
tmux select-pane -t $SESSION:0.0

tmux attach -t $SESSION
