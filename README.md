# FMD (Internet Radio Service Daemon)

This is a fork from the original [FMD](https://github.com/hzqtc/fmd). The aim of this fork is to provide more advanced features.

## Architectural differences

### Decoder

This version of `fmd` uses `ffmpeg` instead of `mpg123` to decode music. The rational is to support more audio formats e.g. `m4a`.

### Network transfer

Improving on the original model, this version uses a multi-threaded downloading framework to pull content from network. At any time when streaming music from an online service, multiple instances of `curl` keep running and caching songs until the queue becomes empty (the default number of song downloaders is 2 but you can change that number in the source file).

A negativity here is that when the program first starts pulling songs it's relatively slow, since the additional downloaders will take away bandwidth for the first song. However, this will be subsequently complemented by smoother music playing in all following songs. (Or you can simply change the number of assigned song downloaders to 1 to avoid this problem)

## More services available

In addition to the original Douban services, two other types of services are added:

### <a id="local"></a>Local music channel / Red-Heart channel

#### Configuration

To configure the local music channel simply put this in your `fmd.conf`

    [Local]
    music_dir = {default value: ~/Music}

The local channel comes with id `999` and if you also use my fork of [FMC](https://github.com/lynnard/fmc), it will display the channel name as the name of the current login user. All files of mimetype `audio/*` within the `music_dir` are added randomly to the playlist.

You also need to make sure you have installed `mutagen` (for parsing the music tags) and downloaded my frontend [client](mutagen).

#### Like

By default all music is `liked`. If you unrate a song, the action would be the same as `ban`.

#### Ban

The song will be removed from your disk. In addition, if it's enclosed in some directory that becomes empty, that directory is removed as well.

The local music channel is also the fall-back channel if network becomes unavailable i.e. when `fmd` is unable to retrieve any playlists from online channels.

Aside from these things the local channel is almost identical to any online channel.

## Jing.fm

#### Configuration

You need to first have a [Jing.fm](http://jing.fm) account. Then run this line at the command line, replacing `<email>` and `<password>` accordingly.

    $ curl -i -d'email=<email>' -d'pwd=<password>' 'http://jing.fm/api/v1/sessions/create'

Copy down your userid (buried inside the JSON response `result.usr.id`), Jing-A-Token-Header and Jing-R-Token-Header and put them into your `fmd.conf`

    [JingFM]
    atoken = <Jing-A-Token-Header>
    rtoken = <Jing-R-Token-Header>
    uid = <uid>

#### Channels

To start a Jing.fm channel, simply use the old command `setch <string>`. While any integer value will be interpreted as a Douban channel, all other strings will be seen as queries into Jing.fm. For example:

    fmc setch '周杰伦'

will start a new Jing.fm channel on 周杰伦.

#### Like & Ban

These actions are identical to those used with Douban channels from the user point of view. But of course now the data is sent to jing.fm instead.

## More fine-grained infomation

The `info` command will now also give back the `kbps` rate of the current song. This can be different depending on each song.

## More commands

A few more commands are added in this fork. They include

* `kbps <bitrate>`: on-the-fly switching of music quality
* `webpage`: opens the douban music page for the current song using the browser specified in the shell variable `$BROWSER`; if the page url is not available e.g. for Jing.fm channels, it will open the search page on douban music

## Music Caching

All played and liked songs will be saved to `music_dir` (as specified in the [Local](#local) section) in `artist/title.<ext>` format. The ID3 tags (for `m4a`, iTunes-style tags) will be saved along as well. The cover image, when downloadable, will be downloaded and embedded into the song.

Again, to make sure all the tags work you need to install `mutagen` and download my [frontend](mutagen).
