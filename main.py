#! /usr/bin/env python2

import os
import sys
import socket
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
		else:
			config.add_section('DoubanFM')
			config.set('DoubanFM', 'channel', self.channel)
			config.set('DoubanFM', 'uid', self.uid)
			config.set('DoubanFM', 'token', self.token)
			config.set('DoubanFM', 'expire', self.expire)

			config.add_section('Server')
			config.set('Server', 'addr', self.addr)
			config.set('Server', 'port', self.port)

			with open(config_filename, 'wb') as configfile:
				config.write(configfile)

	def run(self):
		self.readConfig()
		self.playlist = Playlist(self.channel, self.uid, self.token, self.expire)
		self.player = Player(on_end = self.playlist.next)

		sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		sock.bind((socket.gethostbyname(self.addr), self.port))
		sock.listen(5)

		while True:
			conn, addr = sock.accept()
			conn.send('OK FMD 1.0\n')
			while True:
				data = conn.recv(1024)
				data = data.strip()
				if data == 'bye':
					conn.close()
					break
				elif data == 'play':
					self.player.play()
					conn.send('OK\n')
				elif data == 'stop':
					self.player.stop()
					conn.send('OK\n')
				elif data == 'skip':
					self.player.stop()
					self.player.setSong(self.playlist.skip())
					self.player.play()
					conn.send('OK\n')
				elif data == 'ban':
					self.player.stop()
					self.player.setSong(self.playlist.ban())
					self.player.play()
					conn.send('OK\n')
				elif data == 'rate':
					self.playlist.rate()
					conn.send('OK\n')
				elif data == 'unrate':
					self.playlist.unrate()
					conn.send('OK\n')
				elif data == 'info':
					conn.send(self.player.info() + '\n')
				else:
					conn.send('unknown command: %s\n' % data)

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
