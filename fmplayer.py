#!/usr/bin/env python2

import time
import thread
import json
import pygst
pygst.require("0.10")
import gst
import os
import urllib

class Player(object):
	def __init__(self, on_end = None, use_cache = False):
		self.current = None
		self.playmode = False
		self.playthread = None
		self.progress = 0
		self.length = 0
		self.on_end = on_end

		self.playbin = gst.element_factory_make("playbin2", "player")

		self.use_cache = use_cache
		self.cache_dir = os.path.expanduser('~/.cache/douban.fm/')
		if self.use_cache:
			if not os.path.exists(self.cache_dir):
				os.makedirs(self.cache_dir)

	def _play(self):
		self.playbin.set_state(gst.STATE_PLAYING)
		self.progress = 0
		threadid = thread.get_ident()
		while self.playmode and self.playthread == threadid:
			time.sleep(1)
			try:
				self.progress = self.playbin.query_position(gst.FORMAT_TIME, None)[0] / 1000000000
				self.length = self.playbin.query_duration(gst.FORMAT_TIME, None)[0] / 1000000000
			except:
				continue
			if self.progress >= self.length:
				if self.on_end:
					self.stop()
					self.setSong(self.on_end())
					self.play()
				break

	def setSong(self, song):
		self.current = song

	def download(self, song):
		filename = self.cache_dir + '%s.mp3' % song.sid
		partial = filename + ".part"

		if os.path.exists(filename) or os.path.exists(partial):
			return

		try:
			open(partial, 'w').close()
			print('Retrieving: %s at %s' % (song.title, song.url))
			urllib.urlretrieve(song.url, partial)
			os.rename(partial, filename)
		except:
			if os.path.exists(partial):
				os.remote(partial)

		# create a friendly symlink to original file
		album_dir = '%s/[%s] %s/' % (song.artist, song.pubdate, song.album)
		if not os.path.exists(self.cache_dir + album_dir):
			os.makedirs(self.cache_dir + album_dir)

		# TODO: fetch cover as needed

		os.symlink('../../%s.mp3' % song.sid, 
			 self.cache_dir + album_dir + song.title + '.mp3')


	def cacheFilter(self, song):
		filename = self.cache_dir + '%s.mp3' % song.sid
		result = song.url

		if os.path.exists(filename):
			# use that file as cache
			result = "file://%s" % filename

		if self.use_cache:
			# background download that song to cache
			thread.start_new_thread(self.download, (song, ))

		return result


	def play(self):
		if not self.current and self.on_end:
			self.setSong(self.on_end())
		if self.current:
			location = self.cacheFilter(self.current)
			print("Now playing: " + location)
			self.playbin.set_property("uri", location)
			self.playmode = True
			self.playthread = thread.start_new_thread(self._play, ())

	def stop(self):
		self.playmode = False
		self.playbin.set_state(gst.STATE_NULL)

	def info(self):
		if not self.playmode:
			return json.dumps({ 'status': 'stopped' })
		else:
			return json.dumps({ 'status': 'playing', 'progress': self.progress, 'length': self.length, 'song': self.current.toObj() })

# vim: noet
