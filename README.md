# FMD (Douban FM Daemon)

FMD stands for *Douban FM Daemon*, inspired by MPD (Music Player Daemon).

FMD plays music from Douban FM in background and communicate with clients through TCP connections.

## Config

The main config file is `~/.fmd/fmd.conf`. Config file includes several sections.

In *DoubanFM* section, there are the following config items:

	channel [int]   # Douban FM Channel id
	uid [string]    # Douban FM user id
    uname [string]  # Douban FM user name
	token [string]  # Douban FM authorization token
	expire [string] # token expire time

To get a complete channel list, try:

	wget -q -O - "http://www.douban.com/j/app/radio/channels" | json_pp

To get your own *uid*, *uname*, *token* and *expire*, try:

	wget -q -O - --post-data="email=[email]&password=[passwd]&app_name=radio_desktop_win&version=100" "http://www.douban.com/j/app/login" | json_pp

Replace *[email]* and *[passwd]* with your douban account and password.

In *Output* section, there are the following config items:

    driver [string] # audio output driver, default is "alsa"
    device [string] # audio output device, can be omitted
    rate [int]      # audio ouput rate, default to 44100

In *Server* section, there are the following config items:

    address [string]# server listen address, default to "localhost"
    port [int]      # server listen port, default to 10098

Please create a config file before using FMD. A sample config file is:

    [DoubanFM]
    channel = 0
    uid = 123456
    uname = username
    token = 1234abcd
    expire = 1345000000

    [Output]
    driver = alsa
    device = default
    rate = 44100

    [Server]
    address = localhost
    port = 10098

As FMD uses [libao](http://xiph.org/ao) for audio output, users may found it
useful to refer to [libao's driver
document](http://www.xiph.org/ao/doc/drivers.html) when deciding which driver to
use. For example, users of PulseAudio should change the Output section to:

    [Output]
    driver = pulse
    device = 0
    rate = 44100

## Protocol

The communication between FMD and clients go throught TCP connection.

Commands client can send are *play*, *stop*, *pause*, *toggle*, *skip*, *ban*, *rate*, *unrate*, *info* and *end*. These commands are all self-explained except *end* will tell FMD server to exit.

FMD responses to most commands are json formmated strings containing current playing infomation.

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

## Install

FMD is written in GNU C and depends on `libcurl`, `json-c`, `mpg123`, `libao` and `alsa`.

## Known issues

Sometimes, FMD may use too much CPU. It's a known issue of `libao` and `alsa`. Trying `fmc stop` and `fmc play` may help.

## Todos

* Add support for DJ channels.
* Add logs.
* Cover more error cases.
* Improve FMD protocol.
