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
	port [int]      # FMD listen port, default to 6600, which is the same as MPD

In "Behavior" section, there are two config values:

	autoplay [bool] # auto play when FMD started
    cache [bool]    # cache music files in ~/.cache/douban.fm/ for later use

## Protocol

The communication between FMD and clients go throught TCP connection.

The protocol is similar to [MPD Protocol](http://mpd.wikia.com/wiki/MusicPlayerDaemonCommands). Following commands are (partially) MPD-compatible:

 * close (bye)
 * command\_list\_begin
 * command\_list\_end
 * command\_list\_ok\_begin
 * currentsong
 * listall
 * listallinfo
 * next
 * pause
 * ping
 * play
 * playid
 * plchanges
 * previous
 * search
 * seek
 * seekid
 * status
 * stop
 * toggle

FMD also recognizes "ban", "rate", "unrate" and "skip", these commands are all self-explained.

## Usage

To start FMD:

	$ python2 fmd/main.py start

You can simplily use `mpc` to control FMD:

    $ mpc play
    $ mpc pause
    $ mpc next
    $ mpc prev

For commands not provided by official `mpc` such as `rate`, `ban`. You can
source `mpc_wrapper.sh` and `mpc` should transparently support these commands
(![socat](http://www.dest-unreach.org/socat/) is required):

    $ . /path/to/fmd/mpc_wrapper.sh
    $ mpc rate
    OK

Or use `telnet` if you are an advanced user:

    $ telnet localhost 6600
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    OK MPD 0.11.0 (FMD 0.1)
    skip
    OK

You can also try other MPD clients, for example, `sonata`.

NOTE: sonata will disable playback controls when playlist is empty and 
sonata will NOT update playlist automatically. You may want to make sure
playlist is not empty by playing something using `mpc play`. Currently, to get
playlist refreshed, you may need restart sonata manually. Playback controls and
seek should work regardless of playlist.

![Sonata](http://p.twimg.com/AgvMGieCMAE5F8E.png)


## Install

FMD is written in python2 and depends on `gstreamer0.10-python`. The recommendation is to install all gstreamer components and plugins.

To try FMD, just clone the repository. Run `python2 fmd/main.py start` and try `mpc`, `sonata` or `telnet` example above. You may also interested in [FMC](https://github.com/hzqtc/fmc), a simple CLI client for FMD.

## Contribute

This project is on its very ealy stage and requires lots of improvements. Any feedbacks, patches, forks and other helps are welcome.
