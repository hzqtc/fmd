#! /usr/bin/env python2

import os
import sys
import socket
import thread
import time
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

	def execute(self, commands, command_list = None):
		response = ''

		for cmd, params in commands:
			ok = True

			if cmd == 'pause' and ('f' in params) or ('0' in params):
				cmd = 'play'
			elif cmd == 'toggle':
				if self.player.stopped or self.player.paused:
					cmd = 'play'
				else:
					cmd = 'pause'
			elif cmd == 'close':
				cmd = 'bye'
			elif cmd == 'previous':
				cmd = 'prev'
			elif cmd == 'playlist':
				cmd = 'playlistinfo'

			# main cmd handler
			if cmd == 'bye':
				return None
			elif cmd in [ 'seekid', 'seek' ]:
				# FIXME: currently ignore songid
				offset = int(params.split()[-1].replace('"',''))
				self.player.seek(offset)
			elif cmd in [ 'play', 'stop', 'pause' ]:
				getattr(self.player, cmd)()
			elif cmd in ['skip', 'next', 'prev', 'ban']:
				song = getattr(self.playlist, cmd)()
				if song:
					self.player.stop()
					self.player.setSong(song)
					self.player.play()
				else:
					ok = False
			elif cmd == 'currentsong':
				song = self.player.current
				if song:
					response += 'file: %s\n' % song.url
					response += 'Time: %s\n' % self.player.length
					response += 'Artist: %s\n' % song.artist
					response += 'Title: %s\n' % song.title
					response += 'Album: %s\n' % song.album
					response += 'Date: %s\n' % song.pubdate
					response += 'Pos: %s\n' % self.playlist.playlist.index(song)
					response += 'Id: %s\n' % song.sid
			elif cmd == 'ping':
				ok = True
			elif cmd in ['rate', 'unrate']:
				if not getattr(self.playlist, cmd)():
					ok = False
			elif cmd == 'status':
				response += 'volume: 100\n'
				response += 'repeat: 1\n'
				response += 'random: 1\n'
				response += 'single: 0\n'
				response += 'playlist: 1\n'
				response += 'playlistlength: %s\n' % len(self.playlist.playlist)
				response += 'xfade: 0\n'
				response += 'mixrampdb: 0.000000\n'
				response += 'mixrampdelay: nan\n'
				response += 'state: %s\n' % self.player.status()
				song = self.player.current

				if song:
					song_index = self.playlist.playlist.index(song)
					response += 'song: %s\n' % song_index
					response += 'songid: %s\n' % song.sid
					response += 'time: %s:%s\n' % (self.player.progress, self.player.length)
					response += 'elapsed: %s\n' % self.player.progress
					response += 'bitrate: 404\n'
					response += 'audio: 44100::24:2\n'
					response += 'nextsong: %s\n' % ((song_index + 1) %
									 len(self.playlist.playlist))
					response += 'nextsongid: %s\n' % self.playlist.getNext().sid

			elif cmd == 'command_list_end':
				command_list = None
			else:
				ok = False

			if ok:
				if command_list == 'list_ok':
					response += 'list_OK\n'
				elif not command_list:
					response += 'OK\n'
			else:
				if cmd == "":
					response += 'ACK [5@0] {} No command given\n'
				else:
					response += 'ACK [5@0] {} unknown command: "%s"\n' % cmd
				if command_list:
					break

		return response.encode('utf-8')

	def handle(self, conn):
		command_list = None
		commands = []

		while True:
			line = ''
			while True:
				char = conn.recv(1)
				if char == '\n':
					break
				elif char == '':
					line = None
					break
				else:
					line += char

			if not line:
				break

			cmd, params = (line.strip().lower().split(' ', 1) + [''])[0:2]
			print("%s" % line)

			if cmd == 'command_list_begin':
				command_list = 'list'
			elif cmd == 'command_list_ok_begin':
				command_list = 'list_ok'
			else:
				commands += [(cmd, params)]

			if command_list and cmd != 'command_list_end':
				continue

			# execute command(s)
			response = self.execute(commands, command_list)
			# print("Response: [[[%s]]]" % response)

			if response:
				conn.send(response)
				command_list = None
				commands = []
			else:
				break

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
			conn.send('OK MPD 0.11.0 (FMD 0.1)\n')
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
