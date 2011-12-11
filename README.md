# FMD (Douban FM Daemon)

FMD stands for "Douban FM Daemon", inspired by MPD (Music Player Daemon).

FMD plays music from Douban FM channels in background and communicate with clients through TCP connection.

## Config

The main config file is '~/.fmd.conf', a default config file will be created when first started.
Config file includes two sections: "DoubanFM" and "Server".

In "DoubanFM" section, there are four config values:

	channel [int]   # Douban FM Channel id
	uid [string]    # Douban FM user id
	token [string]  # Douban FM authorization token
	expire [string] # token expire time

To get a complete channel list, try:
	
	wget -q -O - "http://www.douban.com/j/app/radio/channels" | json_pp

Only `channel` is required, others are only required when `channel = 0` (Private Channel).

To get your own `uid`, `token` and `expore`, try:

	wget -q -O - --post-data="email=[email]&password=[passwd]&app_name=radio_desktop_win&version=100"  "http://www.douban.com/j/app/login" | json_pp

In "Server" section, there are two config values:

	addr [string]   # FMD bind addr, default to `localhost`
	port [int]      # FMD listen port, default to 10098, which is the combination of two ascii code 'd' and 'b'

## Protocol

The communication between FMD and clients go throught TCP connection.

When client connected, FMD will send `OK FMD [version]`.

Commands client can send are "play", "stop", "skip", "ban", "rate", "unrate", "info" and "bye", these commands are all self-explained.
Response to command "info" is a json formmated string. Response to all other commands are "OK" when command execution finished.

The simplest FMD client is telnet:

	$ python2 fmd/main.py start
	$ telnet localhost 10098
	Trying ::1...
	Connection failed: Connection refused
	Trying 127.0.0.1...
	Connected to localhost.
	Escape character is '^]'.
	OK FMD 1.0
	info
	{"status": "stopped"}
	play
	OK
	info
	{"status": "playing", "song": {"album": "/subject/1853585/", "picture": "http://img1.douban.com/mpic/s4715464.jpg", "ssid": "5e96", "albumtitle": "FutureSex/LoveSounds", "url": "http://mr3.douban.com/201112111834/ffe796f3e845701dbd7fe595e19c9eda/view/song/small/p468936.mp3", "company": "Jive", "artist": "Justin Timberlake", "rating_avg": 4.09556, "length": 432, "subtype": "", "public_time": "2006", "aid": "1853585", "sid": "468936", "title": "Sexyback", "like": "0"}}
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

To try FMD, just clone the repository. Run `python2 fmd/main.py start` and try the telnet examples above.

## Thanks for dropping by

This project is on its very ealy stage and requires lots of improvements. Any feedbacks, patches, forks and other helps are welcome.
