# FMD (Douban FM Daemon)

This is a fork from the original [fmc](https://github.com/hzqtc/fmc). The aim of this fork is to provide more advanced features (mostly for paid users).

## More fine-grained infomation

The `info` command will now also give back the `kbps` rate of the current song. This can be different depending on each song.

## More commands

A few more commands are added in this fork. They include

* `kbps <bitrate>`: on-the-fly switching of music quality
* `website`: opens the douban music page for the current song using the browser specified in the shell variable `$BROWSER`

## Music Caching (Only for paid users)

If you are a paid user, you can specify the following values in the config file

    [Download]
    music_dir = {default value: ~/Music}
    tmp_dir = {default value: /tmp}

The paths will be expanded according to shell specification.

All played and liked songs will then be saved to `music_dir` in `artist/title.mp3` format. The ID3 tags will be saved along as well. The cover image, when downloadable, will be downloaded and embedded into the song (if un-downloadable, then the url is saved into the cover image tag).

To make sure all the ID3 tags and cover images work you need to have [eyeD3](http://eyed3.nicfit.net/) installed and reachable via `eyeD3` on the command line.

## Local music channel / Red-Heart channel

A new channel with the login name of the current user is added. All music files ending with `.mp3` in the `music_dir` specified in the previous section are added randomly to the playlist.

### Like

By default all music added is `liked`. If you unrate a song, the action would be the same as `ban`.

### Ban

The song will be removed from your disk. In addition, if it's enclosed in some directory and that directory becomes empty, the directory is removed as well.

Note that all actions are *local* only -- they do not send reports to Douban. This is for the reasons

* faster performance
* when the network is down, playing local songs without any network connection should be desirable

The local music channel is also the fall-back channel if network becomes unavailable i.e. when `fmc` is unable to retrieve any playlists for the online channels it will switch to the local channel instead.

Aside from these things the local music channel is almost identical to any online channel.

To use the local music channel though, you need to have a ID3 tag parser. I've written [one](eyeD3f) and included in this repo.
