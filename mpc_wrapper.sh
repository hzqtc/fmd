# fmd mpc wrapper

# makes
#   mpc ban
#   mpc skip
#   mpc rate
#   ...
# available
#
# needs socat
#
# usage: source this file 

function mpc() {
    if [[ '_ban_skip_rate_unrate_ping_' == *_$1_* ]]; then
        PORT=${MPD_PORT:-6600}
        HOST=${MPD_HOST:-localhost}
        echo "$*""\nbye" | socat stdin!!stdout tcp-connect:$HOST:$PORT | grep -v 'FMD'
    else
        /usr/bin/env mpc "$@"
    fi
}
