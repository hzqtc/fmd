#!/usr/bin/env python2

import StringIO
import urllib
import urllib2
import thread
import json

class Song(object):
	def __init__(self):
		self.title = ''
		self.artist = ''
		self.album = ''
		self.pubdate = ''
		self.cover = ''
		self.url = ''
		self.sid = ''
		self.like = False
		self.state = ''

	def __init__(self, jsonobj):
		self.title = jsonobj['title']
		self.artist = jsonobj['artist']
		self.album = jsonobj['albumtitle']
		self.pubdate = jsonobj['public_time']
		self.cover = jsonobj['picture']
		self.url = jsonobj['url']
		self.sid = jsonobj['sid']
		self.like = True if jsonobj['like'] == '1' else False
		self.state = ''

	def toObj(self):
		obj = {
			'title': self.title,
			'artist': self.artist,
			'album': self.album,
			'pubdate': self.pubdate,
			'cover': self.cover,
			'url': self.url,
			'like': self.like,
		}
		return obj

	def info(self, playlist = None):
		result = self.shortinfo() + \
				'Artist: %s\n' % self.artist + 'Title: %s\n' % self.title + \
				'Album: %s\n' % self.album + 'Date: %s\n' % self.pubdate + \
				'Id: %s\n' % self.sid
		if playlist:
			result += 'Pos: %s\n' % playlist.index(self)
		return result

	def shortinfo(self):
		return 'file: %s\n' % self.url


class Playlist(object):
	def __init__(self, channel, uid, token, expire, playlist_changed = None):
		self.playlist = []
		self.playing = -1

		self.api = 'http://www.douban.com/j/app/radio/people'
		self.app_name = 'radio_desktop_win'
		self.version = '100'
		self.channel = channel
		self.uid = uid
		self.token = token
		self.expire = expire

		self.playlist_change_cb = playlist_changed

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
		self.notifyCallbacks()
		return self.playlist[self.playing]

	def getNext(self):
		if len(self.playlist) > self.playing + 1:
			return self.playlist[self.playing + 1]
		else:
			return self.playlist[0]

	def skip(self):
		if self.playing < 0 or self.playing >= len(self.playlist):
			return None

		self.playlist[self.playing].state = 's'
		del self.playlist[self.playing + 1:]
		self.sendLongReport('s')

		self.playing += 1
		self.notifyCallbacks()
		return self.playlist[self.playing]

	def prev(self):
		self.playing -= 1
		if self.playing == -1:
			self.playing = len(self.playlist) - 1
		return self.playlist[self.playing]

	def ban(self):
		if self.playing < 0 or self.playing >= len(self.playlist):
			return None

		self.playlist[self.playing].state = 'b'
		del self.playlist[self.playing + 1:]
		self.sendLongReport('b')

		self.playing += 1
		self.notifyCallbacks()
		return self.playlist[self.playing]

	def rate(self):
		if self.playing < 0 or self.playing >= len(self.playlist):
			return False

		self.playlist[self.playing].like = True
		self.sendShortReport('r')
		return True

	def unrate(self):
		if self.playing < 0 or self.playing >= len(self.playlist):
			return False

		self.playlist[self.playing].like = False
		self.sendShortReport('u')
		return True

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
		self.notifyCallbacks()

	def sendShortReport(self, action):
		def sendRequest(self, action):
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
		thread.start_new_thread(sendRequest, (self, action, ))

	def setPlaying(self, song):
		try:
			index = self.playlist.index(song)
			if index != self.playing:
				self.playing = index
				self.notifyCallbacks()
		except:
			pass

	def notifyCallbacks(self):
		if self.playlist_change_cb:
			self.playlist_change_cb(self.playlist, self.playing)
# vim: noet
