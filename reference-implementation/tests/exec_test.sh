#!/bin/bash               

THISDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
INFILE="input_file"
OUTFILE="output_file"
# Tuneable params
LINKSIM="${LINKSIM:-$THISDIR/../../LINGI1341-linksim/linksim}"
SENDER="${SENDER:-$THISDIR/../sender}"
RECVER="${RECVER:-$THISDIR/../receiver}"
INFILESRC="${INFILESRC:-/dev/urandom}"
INFILESIZ="${INFILESIZ:-512}"
SNDPORT="${SNDPORT:-1341}"
RCVPORT="${RCVPORT:-1341}"
RCVLOG="${RCVLOG:-receiver.log}"
SNDLOG="${SNDLOG:-sender.log}"
LNKLOG="${LNKLOG:-linksim.log}"
LOSS="${LOSS:-10}"
DELAY="${DELAY:-50}"
JITTER="${JITTER:-5}"
SEED="${SEED:-187623649}"
ERRRATE="${ERRRATE:-2}"


echo "Test parameters: loss=$LOSS delay=$DELAY jitter=$JITTER err=$ERRRATE size=$INFILESIZ"


function kill_ps() {
    while [ $# -gt 0 ]; do
        kill -0 "$1" &> /dev/null && kill -9 "$1" &> /dev/null
        shift
    done
}

function cleanup()                 
{
    kill_ps $receiver_pid $link_pid $sender_pid 
    exit 0                
}                         
trap cleanup SIGINT                                    


function assert_file_not_exists() {
    while [ $# -gt 0 ]; do
        [ -f "$1" ] && unlink "$1"
        shift
    done
}
assert_file_not_exists "$INFILE" "$OUTFILE"

dd "if=$INFILESRC" "of=$INFILE" bs=1 "count=$INFILESIZ" &> /dev/null                                             
"$LINKSIM" -p $SNDPORT -P $RCVPORT -l $LOSS -d $DELAY -e $ERRRATE -R  &> "$LNKLOG" &
link_pid=$!               

"$RECVER" :: $RCVPORT > "$OUTFILE"  2> "$RCVLOG" &
receiver_pid=$!           

sleep .1

if ! "$SENDER" ::1 $SNDPORT < "$INFILE" 2> "$SNDLOG" ; then
    echo "The sender crashed!" 
    cat "$SNDLOG"
fi                        

sleep 3

kill_ps $receiver_pid
kill_ps $link_pid

if ! cmp --silent "$INFILE" "$OUTFILE"; then 
    echo "The transfert corrupted the file, diff is: (expected vs received)"        
    diff -C 9 <(od -Ax -t x1z "$INFILE") <(od -Ax -t x1z "$OUTFILE")
    cat "$SNDLOG"
    cat "$RCVLOG"
    cat "$LNKLOG"
    exit 1                  
else                      
    echo "Success!"
    exit 0
fi
