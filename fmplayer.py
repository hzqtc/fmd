#!/usr/bin/env python2

import time
import thread
import json
import pygst
pygst.require("0.10")
import gst

class Player(object):
	def __init__(self, on_end = None):
		self.current = None
		self.playmode = False
		self.playthread = None
		self.progress = 0
		self.length = 0
		self.on_end = on_end

		self.playbin = gst.element_factory_make("playbin2", "player")

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

	def play(self):
		if not self.current and self.on_end:
			self.setSong(self.on_end())
		if self.current:
			self.playbin.set_property("uri", self.current.url)
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
