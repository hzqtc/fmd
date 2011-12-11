#!/usr/bin/env python2

import StringIO
import urllib
import urllib2
import json

class Song(object):
	def __init__(self):
		self.title = ''
		self.artist = ''
		self.album = ''
		self.length = 0
		self.pubdate = ''
		self.cover = ''
		self.url = ''
		self.sid = ''
		self.state = ''

	def __init__(self, jsonobj):
		self.title = jsonobj['title']
		self.artist = jsonobj['artist']
		self.album = jsonobj['albumtitle']
		self.length = jsonobj['length']
		self.pubdate = jsonobj['public_time']
		self.cover = jsonobj['picture']
		self.url = jsonobj['url']
		self.sid = jsonobj['sid']
		self.state = ''

	def toObj(self):
		obj = {
			'title': self.title,
			'artist': self.artist,
			'album': self.album,
			'length': self.length,
			'pubdate': self.pubdate,
			'cover': self.cover,
			'url': self.url,
		}
		return obj

class Playlist(object):
	def __init__(self, channel, uid, token, expire):
		self.playlist = []
		self.playing = -1

		self.api = 'http://www.douban.com/j/app/radio/people'
		self.app_name = 'radio_desktop_win'
		self.version = '100'
		self.channel = channel
		self.uid = uid
		self.token = token
		self.expire = expire

	def history(self):
		history = StringIO.StringIO()
		i = len(self.playlist) - 1
		c = 0
		while i >= 0 and c < 10:
			song = self.playlist[i]
			if song.state:
				history.write('|%s:%s' % (song.sid, song.state))
				c += 1
		return history.getvalue()

	def next(self):
		if self.playing >= 0:
			self.playlist[self.playing].state = 'e'
			self.sendShortReport('e')

		if len(self.playlist) == 0:
			self.sendLongReport('n')
		elif len(self.playlist) - 1 == self.playing:
			self.sendLongReport('p')
		self.playing += 1
		return self.playlist[self.playing]

	def skip(self):
		self.playlist[self.playing].state = 's'
		del self.playlist[self.playing + 1:]
		self.sendLongReport('s')

		self.playing += 1
		return self.playlist[self.playing]

	def ban(self):
		self.playlist[self.playing].state = 'b'
		del self.playlist[self.playing + 1:]
		self.sendLongReport('b')

		self.playing += 1
		return self.playlist[self.playing]

	def rate(self):
		self.sendShortReport('r')

	def unrate(self):
		self.sendShortReport('u')

	def sendLongReport(self, action):
		params = {
			'app_name': self.app_name,
			'version': self.version,
			'user_id': self.uid,
			'expire': self.expire,
			'token': self.token,
			'channel': self.channel,
			'sid': self.playlist[self.playing].sid if self.playing >= 0 else '',
			'h': self.history(),
			'type': action,
		}
		url = '%s?%s' % (self.api, urllib.urlencode(params))
		f = urllib2.urlopen(url)
		obj = json.load(f)
		for s in obj['song']:
			try:
				song = Song(s)
				self.playlist.append(song)
			except KeyError:
				continue

	def sendShortReport(self, action):
		params = {
			'app_name': self.app_name,
			'version': self.version,
			'user_id': self.uid,
			'expire': self.expire,
			'token': self.token,
			'channel': self.channel,
			'sid': self.playlist[self.playing].sid if self.playing >= 0 else '',
			'type': action,
		}
		url = '%s?%s' % (self.api, urllib.urlencode(params))
		urllib2.urlopen(url)

