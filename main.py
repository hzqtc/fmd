#! /usr/bin/env python2

import os
import sys
import socket
import thread
import ConfigParser

from fmplaylist import Playlist
from fmplayer import Player
from daemon import Daemon

class FMDaemon(Daemon):
	def __init__(self):
		super(FMDaemon, self).__init__(pidfile = '/tmp/fmd.pid')
		self.channel = 1
		self.uid = ''
		self.token = ''
		self.expire = ''
		self.addr = 'localhost'
		self.port = 10098
		self.autoplay = False

	def readConfig(self):
		config = ConfigParser.RawConfigParser()
		config_filename = os.path.expanduser('~/.fmd.conf')
		if config.read(config_filename):
			if config.has_section('DoubanFM'):
				self.channel = config.getint('DoubanFM', 'channel')
				self.uid = config.get('DoubanFM', 'uid')
				self.token = config.get('DoubanFM', 'token')
				self.expire = config.get('DoubanFM', 'expire')
			if config.has_section('Server'):
				self.addr = config.get('Server', 'addr')
				self.port = config.getint('Server', 'port')
			if config.has_section('Behavior'):
				self.autoplay = config.getboolean('Behavior', 'autoplay')
		else:
			config.add_section('DoubanFM')
			config.set('DoubanFM', 'channel', self.channel)
			config.set('DoubanFM', 'uid', self.uid)
			config.set('DoubanFM', 'token', self.token)
			config.set('DoubanFM', 'expire', self.expire)

			config.add_section('Server')
			config.set('Server', 'addr', self.addr)
			config.set('Server', 'port', self.port)
			config.set('Behavior', self.autoplay)

			with open(config_filename, 'wb') as configfile:
				config.write(configfile)

	def handle(self, conn):
		while True:
			data = conn.recv(1024)
			data = data.strip()
			if data == 'bye':
				conn.close()
				break
			elif data == 'play':
				self.player.play()
				conn.send('OK')
			elif data == 'stop':
				self.player.stop()
				conn.send('OK')
			elif data == 'skip':
				song = self.playlist.skip()
				if song:
					self.player.stop()
					self.player.setSong(song)
					self.player.play()
					conn.send('OK')
				else:
					conn.send('Failed')
			elif data == 'ban':
				song = self.playlist.ban()
				if song:
					self.player.stop()
					self.player.setSong(song)
					self.player.play()
					conn.send('OK')
				else:
					conn.send('Failed')
			elif data == 'rate':
				if self.playlist.rate():
					conn.send('OK')
				else:
					conn.send('Failed')
			elif data == 'unrate':
				if self.playlist.unrate():
					conn.send('OK')
				else:
					conn.send('Failed')
			elif data == 'info':
				conn.send(self.player.info())
			else:
				conn.send('unknown command: %s' % data)

	def run(self):
		self.readConfig()
		self.playlist = Playlist(self.channel, self.uid, self.token, self.expire)
		self.player = Player(on_end = self.playlist.next)

		sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		sock.bind((socket.gethostbyname(self.addr), self.port))
		sock.listen(5)

		if self.autoplay:
			self.player.play()

		while True:
			conn, addr = sock.accept()
			conn.send('OK FMD 1.0')
			thread.start_new_thread(self.handle, (conn, ))

if __name__ == '__main__':
	fmd = FMDaemon()
	if len(sys.argv) == 2:
		if 'start' == sys.argv[1]:
			fmd.start()
		elif 'stop' == sys.argv[1]:
			fmd.stop()
		elif 'restart' == sys.argv[1]:
			fmd.restart()
		else:
			print "Unknown command"
			sys.exit(2)
		sys.exit(0)
	else:
		print "usage: %s start|stop|restart" % sys.argv[0]
		sys.exit(2)

# vim: noet
