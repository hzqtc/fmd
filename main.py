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
		self.cache = True

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
				self.cache = config.getboolean('Behavior', 'cache')
		else:
			print >> sys.stderr, 'Warning: creating default configure file.'
			config.add_section('DoubanFM')
			config.set('DoubanFM', 'channel', self.channel)
			config.set('DoubanFM', 'uid', self.uid)
			config.set('DoubanFM', 'token', self.token)
			config.set('DoubanFM', 'expire', self.expire)

			config.add_section('Server')
			config.set('Server', 'addr', self.addr)
			config.set('Server', 'port', self.port)

			config.add_section('Behavior')
			config.set('Behavior', 'autoplay', self.autoplay)
			config.set('Behavior', 'cache', self.cache)

			with open(config_filename, 'wb') as configfile:
				config.write(configfile)

	def playlistChanged(self, playlist, current):
		if len(playlist) > current + 1 and (current >= 0 or not self.autoplay):
			self.player.cacheFilter(playlist[current + 1])

	def handle(self, conn):
		while True:
			data = conn.recv(1024)
			[ cmd, params ] = data.strip().lower().split(' ', 1)

			if cmd == 'pause' and ('f' in params) or ('0' in params):
				cmd = 'play'
			elif cmd == 'toggle':
				if self.player.stopped or self.player.paused:
					cmd = 'play'
				else:
					cmd = 'pause'
			elif cmd == 'previous':
				cmd = 'prev'
			elif cmd == 'playlist':
				cmd = 'playlistinfo'

			# main cmd handler
			if cmd == 'bye':
				conn.close()
				break
			elif cmd in [ 'play', 'stop', 'pause' ]:
				getattr(self.player, cmd)()
				conn.send('OK')
			elif cmd in ['skip', 'next', 'prev', 'ban']
				song = getattr(self.playlist, cmd)()
				if song:
					self.player.stop()
					self.player.setSong(song)
					self.player.play()
					conn.send('OK')
				else:
					conn.send('Failed')
			elif cmd in ['rate', 'unrate']:
				if getattr(self.playlist, cmd)():
					conn.send('OK')
				else:
					conn.send('Failed')
			elif cmd == 'status':
				conn.send('volume: 1\n')
				conn.send('repeat: 1\n')
				conn.send('random: 1\n')
				conn.send('single: 0\n')
				conn.send('playlist: 1\n')
				conn.send('playlistlength: %s\n' % len(self.Playlist.playlist))
				conn.send('state: %s\n' % self.Player.status())
				conn.send('OK')
			else:
				conn.send('unknown command: "%s"' % cmd)
			conn.send('\n')


	def run(self):
		self.playlist = Playlist(self.channel, self.uid, self.token, self.expire, self.playlistChanged)
		self.player = Player(on_end = self.playlist.next, use_cache = self.cache)

		sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		sock.bind((socket.gethostbyname(self.addr), self.port))
		sock.listen(5)

		if self.autoplay:
			self.player.play()

		while True:
			conn, addr = sock.accept()
			conn.send('OK FMD 1.0\n')
			thread.start_new_thread(self.handle, (conn, ))

if __name__ == '__main__':
	fmd = FMDaemon()
	if len(sys.argv) == 2:
		if 'start' == sys.argv[1]:
			fmd.readConfig()
			fmd.start()
		elif 'stop' == sys.argv[1]:
			fmd.stop()
		elif 'restart' == sys.argv[1]:
			fmd.restart()
		elif 'run' == sys.argv[1]:
			fmd.readConfig()
			fmd.run()
		else:
			print "Unknown command"
			sys.exit(2)
		sys.exit(0)
	else:
		print "usage: %s start|stop|restart" % sys.argv[0]
		sys.exit(2)

# vim: noet
