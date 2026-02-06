#!/bin/bash
# view_attacks.sh - Helper script for viewing attack logs
#
# Usage:
#   ./view_attacks.sh [options]
#
# Options:
#   -f, --follow    Follow log in real-time (like tail -f)
#   -n NUM          Show last NUM attacks (default: 10)
#   -s, --stats     Show attack statistics summary
#   -c, --clear     Clear attack log (requires confirmation)
#   -h, --help      Show this help message

LOG_FILE="/tmp/nn-ebpf-attacks.log"

show_help() {
    cat << EOF
NN-eBPF Attack Log Viewer
=========================

Usage: $0 [OPTIONS]

Options:
  -f, --follow       Follow log in real-time (like tail -f)
  -n NUM             Show last NUM attacks (default: 10)
  -s, --stats        Show attack statistics summary
  -c, --clear        Clear attack log (requires confirmation)
  -h, --help         Show this help message

Examples:
  $0                 # View last 10 attacks
  $0 -n 50           # View last 50 attacks
  $0 -f              # Follow log in real-time
  $0 -s              # Show statistics

Log file location: $LOG_FILE
EOF
}

show_stats() {
    if [ ! -f "$LOG_FILE" ]; then
        echo "No attack log found at $LOG_FILE"
        return
    fi

    echo "NN-eBPF Attack Statistics"
    echo "========================="
    echo ""

    # Total attacks
    total=$(grep -c "^ATTACK DETECTED #" "$LOG_FILE")
    echo "Total Attacks Detected: $total"

    if [ "$total" -eq 0 ]; then
        echo "No attacks recorded yet."
        return
    fi

    echo ""

    # Top attacking IPs
    echo "Top 10 Attacking IPs:"
    grep "^Source IP:" "$LOG_FILE" | awk '{print $NF}' | sort | uniq -c | sort -rn | head -10 | \
        awk '{printf "  %3d attacks - %s\n", $1, $2}'

    echo ""

    # Top targeted ports
    echo "Top 10 Targeted Ports:"
    grep "^Destination Port:" "$LOG_FILE" | awk '{print $NF}' | sort | uniq -c | sort -rn | head -10 | \
        awk '{printf "  %3d attacks - Port %s\n", $1, $2}'

    echo ""

    # Confidence distribution
    echo "Confidence Distribution:"
    grep "^Confidence Level:" "$LOG_FILE" | awk '{print $3}' | sort | uniq -c | sort -rn | \
        awk '{printf "  %3d attacks - %s\n", $1, $2}'

    echo ""

    # Average attack probability
    avg_prob=$(grep "^Attack Probability:" "$LOG_FILE" | awk '{sum += $3; count++} END {if (count > 0) printf "%.1f", sum/count}')
    echo "Average Attack Probability: ${avg_prob}%"

    echo ""

    # Time range
    first=$(grep "^Timestamp:" "$LOG_FILE" | head -1 | awk '{print $2, $3}')
    last=$(grep "^Timestamp:" "$LOG_FILE" | tail -1 | awk '{print $2, $3}')
    echo "First Attack: $first"
    echo "Last Attack:  $last"
}

show_recent() {
    local num_attacks=${1:-10}

    if [ ! -f "$LOG_FILE" ]; then
        echo "No attack log found at $LOG_FILE"
        echo "Make sure attack_monitor is running!"
        return
    fi

    # Count total attacks
    local total=$(grep -c "^ATTACK DETECTED #" "$LOG_FILE")

    if [ "$total" -eq 0 ]; then
        echo "No attacks recorded yet."
        return
    fi

    echo "Showing last $num_attacks of $total total attacks"
    echo "================================================"
    echo ""

    # Show last N attacks (each attack ends with a blank line after performance section)
    tac "$LOG_FILE" | awk -v count=$num_attacks '
        /^========================================$/ && /ATTACK DETECTED/ {
            if (attacks >= count) exit;
            attacks++;
        }
        {lines[NR] = $0}
        END {
            for (i=NR; i>=1; i--) print lines[i]
        }
    '
}

follow_log() {
    if [ ! -f "$LOG_FILE" ]; then
        echo "Waiting for attack log at $LOG_FILE..."
        echo "Make sure attack_monitor is running!"
        echo ""
    fi

    echo "Following attack log in real-time (Ctrl+C to stop)..."
    echo ""

    tail -f "$LOG_FILE" 2>/dev/null || {
        # If file doesn't exist, wait for it
        while [ ! -f "$LOG_FILE" ]; do
            sleep 1
        done
        tail -f "$LOG_FILE"
    }
}

clear_log() {
    if [ ! -f "$LOG_FILE" ]; then
        echo "No log file to clear."
        return
    fi

    local total=$(grep -c "^ATTACK DETECTED #" "$LOG_FILE" 2>/dev/null || echo "0")

    echo "WARNING: This will delete all $total recorded attacks!"
    read -p "Are you sure? (yes/no): " confirm

    if [ "$confirm" = "yes" ]; then
        > "$LOG_FILE"
        echo "Attack log cleared."
    else
        echo "Cancelled."
    fi
}

# Parse command-line arguments
case "$1" in
    -h|--help)
        show_help
        ;;
    -s|--stats)
        show_stats
        ;;
    -f|--follow)
        follow_log
        ;;
    -c|--clear)
        clear_log
        ;;
    -n)
        if [ -z "$2" ]; then
            echo "Error: -n requires a number"
            exit 1
        fi
        show_recent "$2"
        ;;
    "")
        show_recent 10
        ;;
    *)
        echo "Unknown option: $1"
        echo "Use -h or --help for usage information"
        exit 1
        ;;
esac
