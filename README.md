# FMD (Douban FM Daemon)

FMD stands for "Douban FM Daemon", inspired by MPD (Music Player Daemon).

FMD plays music from Douban FM channels in background and communicate with clients through TCP connection.

## Config

The main config file is "~/.fmd.conf", a default config file will be created when first started.
Config file includes three sections: "DoubanFM", "Server" and "Behavior".

In "DoubanFM" section, there are four config values:

	channel [int]   # Douban FM Channel id
	uid [string]    # Douban FM user id
	token [string]  # Douban FM authorization token
	expire [string] # token expire time

To get a complete channel list, try:
	
	wget -q -O - "http://www.douban.com/j/app/radio/channels" | json_pp

Only "channel" is required, others are only necessary when `channel = 0` (Private Channel).

To get your own "uid", "token" and "expire", try:

	wget -q -O - --post-data="email=[email]&password=[passwd]&app_name=radio_desktop_win&version=100" "http://www.douban.com/j/app/login" | json_pp

Replace "[email]" and "[passwd]" with your douban account and password.

In "Server" section, there are two config values:

	addr [string]   # FMD bind addr, default to "localhost"
	port [int]      # FMD listen port, default to 10098, which is the combination of two ascii code 'd' and 'b'

In "Behavior" section, there are two config values:

	autoplay [bool] # auto play when FMD started
    cache [bool]    # cache music files in ~/.cache/douban.fm/ for later use

## Protocol

The communication between FMD and clients go throught TCP connection.

The protocol is similar to [MPD Protocol](http://mpd.wikia.com/wiki/MusicPlayerDaemonCommands). Following commands are MPD-compatible:

 * play
 * pause
 * toggle
 * stop
 * next
 * previous
 * ping
 * currentsong
 * seek
 * status
 * close (bye)
 * command\_list\_begin
 * command\_list\_ok\_begin
 * command\_list\_end

FMD also recognizes "ban", "rate", "unrate" and "info", these commands are all self-explained.
Response to command "info" is a json formmated string.

You can simplily use `mpc` to control FMD:

	$ python2 fmd/main.py start

    $ mpc play -p 10098
    $ mpc pause -p 10098
    $ mpc next -p 10098
    $ mpc prev -p 10098

For commands not provided by `mpc`, you can just use `telnet`:

    $ python2 fmd/main.py start
    $ telnet localhost 10098
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    OK MPD 0.11.0 (FMD 0.1)
    info
    {"status": "stopped"}
    play
    OK
    info
    {"status": "playing", "song": {"album": "/subject/1853585/", "picture": "http://img1.douban.com/mpic/s4715464.jpg", "ssid": "5e96", "albumtitl
    stop
    OK
    info
    {"status": "stopped"}
    bye
    OK
    Connection closed by foreign host.
    $ python2 fmd/main.py stop


## Install

FMD is written in python2 and depends on `gstreamer0.10-python`. The recommendation is to install all gstreamer components and plugins.

To try FMD, just clone the repository. Run `python2 fmd/main.py start` and try `mpc` or `telnet` example above. You may also interested in [FMC](https://github.com/hzqtc/fmc), a simple CLI client for FMD.

## Contribute

This project is on its very ealy stage and requires lots of improvements. Any feedbacks, patches, forks and other helps are welcome.
