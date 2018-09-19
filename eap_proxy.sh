#!/bin/sh
PROXY_PID=0

#0 = true, 1 = false

while true ; do
    # get gateway, may be empty string if we don't have one
    GW="$(ip route show default | awk '/default/{print $3}')"
    if ! ping "$GW" -c 1 -W 1 ; then
	# we need to run the proxy
	if [ "$PROXY_PIX" -eq 0 || kill -0 "$PROXY_PID" ]; then
	    # run proxy
	    echo launching proxy
	    echo log this to a file
	    sleep 2 &
	    PROXY_PID="$!"
	else
	    echo proxy already running
	fi
    else
	# ping worked! Time to tear down the proxy
	if "$PROXY_PID" -ne 0 && kill "$PROXY_PID" ; then
	    echo killed the proxy
	    PROXY_PID="0"  # some invalid PID
	fi
    fi
    sleep 2  # don't work too hard
done

echo starting bg
sleep 1 &
SPID="$!"
echo PID: $SPID
kill $SPID >/dev/null 2>&1
echo killed $?
kill $SPID >/dev/null 2>&1
echo killed $?
