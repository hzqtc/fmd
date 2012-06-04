# FMD (Douban FM Daemon)

FMD stands for "Douban FM Daemon", inspired by MPD (Music Player Daemon).

FMD plays music from Douban FM in background and communicate with clients through TCP connection.

## Config

The main config file is "~/.fmd/fmd.conf". Config file includes one section: "DoubanFM".

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

Please create a config file before using FMD. A sample config file is:

    [DoubanFM]
    channel = 0
    uid = 123456
    uname = username
    token = 1234abcd
    expire = 1345000000

Change the config values as described above.

## Protocol

The communication between FMD and clients go throught TCP connection.

Commands client can send are "play", "stop", "pause", "toggle", "skip", "ban", "rate", "unrate", "info" and "end", these commands are all self-explained. Only notice that "end" will tell FMD to quit and close all client connections.

No response to command "end". Responses to all other commands are json formmated strings containing current playing infomation.

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
    end
    Connection closed by foreign host.


## Install

FMD is written in GNU C and depends on `libcurl`, `libjson`, `mpg123`, `libao` and `alsa`.
