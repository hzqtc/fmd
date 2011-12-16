#!/usr/bin/env python2

import time
import thread
import threading
import json
import pygst
pygst.require("0.10")
import gst
import os
import urllib

class Player(object):
	def __init__(self, on_end = None, use_cache = False):
		self.current = None
		self.stopped = True
		self.paused = False
		self.progress = 0
		self.length = 0
		self.on_end = on_end

		self.playbin = gst.element_factory_make("playbin2", "player")

		self.use_cache = use_cache
		self.cache_dir = os.path.expanduser('~/.cache/douban.fm/')
		if self.use_cache:
			if not os.path.exists(self.cache_dir):
				os.makedirs(self.cache_dir, 0755)

		self.watch_thread = thread.start_new_thread(self._watch_progress, ())
		self.download_thread_lock = threading.Lock()

	def _watch_progress(self):
		self.progress = 0
		last_progress = 0
		lag_counter = 0

		while True:
			time.sleep(0.2)

			if self.paused or self.stopped:
				continue

			try:
				self.progress = self.playbin.query_position(gst.FORMAT_TIME, None)[0] / 1000000000
				self.length = self.playbin.query_duration(gst.FORMAT_TIME, None)[0] / 1000000000
			except:
				pass

			if self.progress == last_progress and not self.paused and \
			not self.stopped and self.progress > 0:
				lag_counter += 1
			else:
				lag_counter = 0

			# state = self.playbin.get_state()[1]
			# print("watch: %s : %s | %s | %s" % (self.progress, self.length, lag_counter, state))

			if (self.progress >= self.length and self.length > 0) or lag_counter > 30:
				if self.on_end:
					lag_counter = 0
					self.stop()
					self.current = None
					self.play(True)
				else:
					self.stop()
					self.play(True)

			last_progress = self.progress

	def setSong(self, song, fm_playlist = None):
		if self.current and self.current.sid == song.sid:
			return

		self.stop()
		self.current = song
		if fm_playlist:
			fm_playlist.setPlaying(song)
		self.play()

	def download(self, song):
		filename = self.cache_dir + '%s.mp3' % song.sid
		partial = filename + ".part"

		with self.download_thread_lock:
			try:
				if os.path.exists(filename) or os.path.exists(partial):
					return
				else:
					open(partial, 'w').close()
			except:
				pass

		try:
			print('Retrieving: %s at %s' % (song.title, song.url))
			urllib.urlretrieve(song.url, partial)
		except:
			if os.path.exists(partial):
				os.remove(partial)
				return

		# verify mp3 file, file too small should not be valid mp3
		if os.path.getsize(partial) < 60000:
			os.remove(partial)
			return
		else:
			os.rename(partial, filename)

		# create a friendly symlink to original file
		h = lambda x: x.replace('/', '_').replace('.','').strip()
		album_dir = '%s/[%s] %s/' % (
			h(song.artist), h(song.pubdate), h(song.album))
		if not os.path.exists(self.cache_dir + album_dir):
			os.makedirs((self.cache_dir + album_dir), 0755)

		# TODO: fetch cover as needed

		print("symlink: " + self.cache_dir + album_dir + h(song.title) + '.mp3')

		os.symlink('../../%s.mp3' % song.sid, 
			 self.cache_dir + album_dir + h(song.title) + '.mp3')


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


	def play(self, rewind = False):
		if not self.current and self.on_end:
			self.current = self.on_end()

		if self.current:
			location = self.cacheFilter(self.current)
			print("Now playing: " + location)
			self.playbin.set_property("uri", location)

		if rewind:
			self.playbin.seek_simple(gst.FORMAT_TIME, gst.SEEK_FLAG_FLUSH, 0)

		self.stopped = False
		self.paused = False
		self.playbin.set_state(gst.STATE_PLAYING)


	def stop(self):
		self.stopped = True
		self.paused = False
		self.playbin.set_state(gst.STATE_NULL)


	def pause(self):
		self.paused = True
		self.stopped = False
		self.playbin.set_state(gst.STATE_PAUSED)


	def status(self):
		if self.paused:
			return "pause"
		elif self.stopped:
			return "stop"
		else:
			return "play"

	def seek(self, seek_sec):
		if self.paused or self.stopped:
			return False

		print("Trying seek: %s" % seek_sec)
		seek_ns = seek_sec * 1000000000
		self.playbin.seek_simple(gst.FORMAT_TIME, gst.SEEK_FLAG_FLUSH, seek_ns)
		return True

	def info(self):
		if self.stopped:
			return json.dumps({ 'status': 'stopped' })
		else:
			return json.dumps({ 'status': 'playing', 'progress': self.progress, 'length': self.length, 'song': self.current.toObj() })

# vim: noet
