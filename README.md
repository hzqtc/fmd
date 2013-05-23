# FMD (Douban FM Daemon)

FMD stands for Douban FM Daemon, inspired by MPD (Music Player Daemon). FMD plays music from Douban FM in background and communicate with clients through TCP connections.

## Config

The main config file is `~/.fmd/fmd.conf`. Config file includes several sections.

In `DoubanFM` section, there are the following config items:

	channel [int]   # Douban FM Channel id
	uid [string]    # Douban FM user id
    uname [string]  # Douban FM user name
	token [string]  # Douban FM authorization token
	expire [string] # token expire time
    kbps [int]      # Douban FM bitrate (only applies to Pro user)

To get a complete channel list, try:

	wget -q -O - "http://www.douban.com/j/app/radio/channels" | json_pp

To get your own `uid`, `uname`, `token` and `expire`, try:

	wget -q -O - --post-data="email=[email]&password=[passwd]&app_name=radio_desktop_win&version=100" "http://www.douban.com/j/app/login"

Replace `[email]` and `[passwd]` with your douban account and password.

In `Output` section, there are the following config items:

    driver [string] # audio output driver, default is "alsa"
    device [string] # audio output device, can be omitted
    rate [int]      # audio ouput rate, default to 44100

As FMD uses [libao](http://xiph.org/ao) for audio output, users may found it useful to refer to [libao's driver document](http://www.xiph.org/ao/doc/drivers.html) when deciding which driver to use. For example, users of PulseAudio should change the `Output` section to:

    [Output]
    driver = pulse
    device = 0
    rate = 44100

And for Mac users:

    [Output]
    driver = macosx
    device =
    rate = 44100

In `Server` section, there are the following config items:

    address [string]# server listen address, default to "localhost"
    port [int]      # server listen port, default to 10098

Please create a config file before using FMD. A sample config file is:

    [DoubanFM]
    channel = 0
    uid = 123456
    uname = username
    token = 1234abcd
    expire = 1345000000
    kbps = 64

    [Output]
    driver = alsa
    device = default
    rate = 44100

    [Server]
    address = localhost
    port = 10098

## Commands

The communication between FMD and clients go throught TCP connection.

Commands client can send are `play`, `stop`, `pause`, `toggle`, `skip`, `ban`, `rate`, `unrate`, `info`, `channels`, `setch` and `end`.

* `play`: start playing
* `stop`: stop playing, and set play position to 0:00
* `pause`: stop playing
* `toggle`: toggle between playing and pause
* `skip`: skip current song
* `ban`: mark current song as "dislike"
* `rate`: mark current song as "like"
* `unrate`: unmark current song
* `info`: simply get FMD info
* `channels`: list available channels
* `setch`: change channel on the fly
* `end`: tell FMD to exit

**Note**: The recommand way to exit FMD is `killall fmd`. Because the `end` command must be sent from the client and will left the FMD port in wait-close for several minutes, during which time new FMD instance cannot bind to the port.

## Protocol

FMD responses to all commands except `end`, the reponse is a json string containing current FMD infomation.

    {
       "len" : 0,
       "sid" : 967698,
       "status" : "play",
       "channel" : 0,
       "like" : 0,
       "artist" : "花儿乐队",
       "album" : "幸福的旁边",
       "cover" : "http://img1.douban.com/mpic/s4433542.jpg",
       "url" : "/subject/1404476/",
       "user" : "小强",
       "pos" : 5,
       "title" : "别骗我",
       "year" : 1999
    }

The simplest FMD client is telnet:

    telnet localhost 10098
    Trying ::1...
    Connection failed: Connection refused
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    info
    {"status":"play","channel":0,"user":"小强","title":"What's My Name (Intro #1)","artist":"Rihanna / Drake", "album":"Promo Only Rhythm...","year":2010,"cover":"http://img1.douban.com/mpic/s4615061.jpg","url":"/subject/5951920/","sid":1561924,"like":0,"pos":107,"len":254}
    toggle
    {"status":"pause","channel":0,"user":"小强","title":"What's My Name (Intro #1)","artist":"Rihanna / Drake", "album":"Promo Only Rhythm...","year":2010,"cover":"http://img1.douban.com/mpic/s4615061.jpg","url":"/subject/5951920/","sid":1561924,"like":0,"pos":111,"len":254}
    toggle
    {"status":"play","channel":0,"user":"小强","title":"What's My Name (Intro #1)","artist":"Rihanna / Drake", "album":"Promo Only Rhythm...","year":2010,"cover":"http://img1.douban.com/mpic/s4615061.jpg","url":"/subject/5951920/","sid":1561924,"like":0,"pos":111,"len":254}
    help
    {"status":"error","message":"wrong command: help"}

[FMC](https://github.com/hzqtc/fmc) is a command line client for FMD.

## Build

FMD is written in C and depends on `libcurl` (for api calls and music downloading), `json-c` (for API parsing), `mpg123` (for music decoding) and `libao` (for music playing).

Currently, there is no binary distribution for this project. So compile from source is the only option. FMD has been tested on Linux and Mac OS X 10.7.

    git clone https://github.com/hzqtc/fmd.git
    cd fmd
    make release

## Utilities for Linux

To generate you configure file automaticly, change your douban account & password in `fmd-update-conf.sh`, then:

    mkdir -p ~/.fmd
    ./fmd-update-conf.sh > ~/.fmd/fmd.conf

Even more, to run fmd on boot in Ubuntu (and some other Linux distribution with init.d), run as root:

    ./install-ubuntu-service.sh
