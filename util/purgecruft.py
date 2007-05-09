#!/usr/bin/python
# vi:ts=2 sw=2 si

# purgecruft.py
# by : r3nh03k

# With torrent after torrent gets downloaded to the same directory, old files
# that the Bittorrent software ignores begins to accumulate.  Yes, a nice rom
# manager would help this problem, but as there really isn't a usable rom
# manager on Linux, this utility will have to suffice.

import sys
import getopt
import os
import sha

class TorrentData:
	"Data type for one torrent file"
	data = None

	def __init__(self, torrentPath):
		self.readTorrent(torrentPath)

	def readTorrent(self, torrentPath):
		tf = open(torrentPath, "rb")
		self.data = self.parseElement(tf)

	# Generic element parser
	def parseElement(self, tf):
		"Don't know what the data is, but we're about to find out"
		dataType = tf.read(1)

		if dataType == 'd':
			"Dictionary!"
			return self.parseDictionary(tf)
		elif dataType == 'l':
			"List!"
			return self.parseList(tf)
		elif dataType == 'i':
			"Integer!"
			return self.parseInteger(tf)
		else:
			length = 0
			while dataType != ':':
				length = length * 10 + int(dataType, 10)
				dataType = tf.read(1)
			return tf.read(length)

	# Parses an integer (the 'i' already removed)
	def parseInteger(self, tf):
		value = 0l
		nextChar = tf.read(1)
		while nextChar != 'e':
			value = value * 10 + int(nextChar, 10)
			nextChar = tf.read(1)
		return value

	# Parses a list (the 'l' already removed)
	def parseList(self, tf):
		list = []
		currChar = tf.read(1)
		while currChar != 'e':
			tf.seek(-1, 1)
			list.append(self.parseElement(tf))
			currChar = tf.read(1)
		return list

	# Parses a dictionary (the 'd' already removed)
	def parseDictionary(self, tf):
		dict = {}
		currChar = tf.read(1)
		while currChar != 'e':
			tf.seek(-1, 1)
			key = self.parseElement(tf)
			value = self.parseElement(tf)
			dict[key] = value
			currChar = tf.read(1)
		return dict

class PurgeDirCruftFromTorrent:
	"Purge the cruft within a directory using a torrent file"
	fileList = []
	pieceList = []
	pieceLen = 0L
	invalidList = []
	filesWithGood = []
	basePath = None
	badPieceCount = 0

	def __init__(self, torrentPath, filesDir):
		self.parseTorrent(TorrentData(torrentPath), filesDir)

	def parseTorrent(self, td, dir):
		# Only use the name if the path exists, otherwise just use the explicit path
		self.basePath = os.path.join(dir, td.data['info']['name'])
		if os.path.isdir(self.basePath) == False:
			self.basePath = dir

		# Build a file list, in order
		for file in td.data['info']['files']:
			path = self.basePath
			for pathElem in file['path']:
				path = os.path.join(path, pathElem)

			self.fileList.append((path, file['length']))
		
		# Now break up the sha1 hashs
		hashIndex = 0
		while hashIndex < len(td.data['info']['pieces']):
			self.pieceList.append(td.data['info']['pieces'][hashIndex:hashIndex + 20])
			hashIndex = hashIndex + 20

		self.pieceLen = td.data['info']['piece length']

	def findInvalidFiles(self):
		filesInCurrPiece = []
		sha1Hasher = sha.new()
		pieceUsed = 0L
		pieceIndex = 0
		filesProcessed = 0

		for fileName, fileSize in self.fileList:
			fileIndex = 0
			fileChunk = None

			try:
				fileData = open(fileName)
			except IOError:
				fileData = None

			while pieceUsed + (fileSize - fileIndex) >= self.pieceLen:
				sizeToRead = self.pieceLen - pieceUsed
				try:
					fileChunk = fileData.read(sizeToRead)
					if len(fileChunk) < sizeToRead:
						fileChunk = fileChunk + '\0' * (sizeToRead - len(fileChunk))
				except:
					fileChunk = '\0' * sizeToRead
					
				sha1Hasher.update(fileChunk)
				filesInCurrPiece.append(fileName)

				if sha1Hasher.digest() != self.pieceList[pieceIndex]:
					self.badPieceCount = self.badPieceCount + 1
					for newInvFile in filesInCurrPiece:
						if newInvFile not in self.invalidList:
							self.invalidList.append(newInvFile)
				else:
					for newGoodFile in filesInCurrPiece:
						if newGoodFile not in self.filesWithGood:
							self.filesWithGood.append(newGoodFile)

				sha1Hasher = sha.new()
				pieceUsed = 0
				fileIndex = fileIndex + sizeToRead
				pieceIndex = pieceIndex + 1
				filesInCurrPiece = []

				print >> sys.stderr, '%(fc)u of %(ft)u files (%(fb)u bad) - %(pc)u of %(pt)u pieces (%(pb)u bad) - %(pct)05.2f%% complete\r' % {'fc': filesProcessed, 'ft': len(self.fileList), 'fb': len(self.invalidList), 'pc': pieceIndex, 'pt': len(self.pieceList), 'pb': self.badPieceCount, 'pct': (100.0 * pieceIndex) / len(self.pieceList)},

			sizeToRead = fileSize - fileIndex
			if sizeToRead > 0:
				try:
					fileChunk = fileData.read(sizeToRead)
					if len(fileChunk) < sizeToRead:
						fileChunk = fileChunk + '\0' * (sizeToRead - len(fileChunk))
				except:
					fileChunk = '\0' * sizeToRead
				
				sha1Hasher.update(fileChunk)
				filesInCurrPiece.append(fileName)
				pieceUsed = pieceUsed + sizeToRead
			filesProcessed = filesProcessed + 1
			print >> sys.stderr, '%(fc)u of %(ft)u files (%(fb)u bad) - %(pc)u of %(pt)u pieces (%(pb)u bad) - %(pct)05.2f%% complete\r' % {'fc': filesProcessed, 'ft': len(self.fileList), 'fb': len(self.invalidList), 'pc': pieceIndex, 'pt': len(self.pieceList), 'pb': self.badPieceCount, 'pct': (100.0 * (pieceIndex + (1.0 * pieceUsed) / self.pieceLen)) / len(self.pieceList)},
		print

	def listInvalid(self):
		print "File with no chance of recovery:"
		for invFile in self.invalidList:
			if invFile not in self.filesWithGood:
				print invFile

		print "Files with some good, but still not quite perfect:"
		for invFile in self.invalidList:
			if invFile in self.filesWithGood:
				print invFile

		justFileList = list(fileElem[0] for fileElem in self.fileList)
		print "Files that simply do not belong:"
		for dirpath, dirnames, filenames in os.walk(self.basePath):
			for filename in filenames:
				filePath = os.path.join(dirpath, filename)
				if filePath not in justFileList:
					print filePath

		print "There are %d bad pieces, or a total of %d data to recover" % (self.badPieceCount, self.pieceLen * self.badPieceCount)

	def backupFileTo(self, origPath, destPath):
		destDir = os.path.dirname(destPath)
		if os.path.isdir(destDir) == False:
			if os.path.lexists(destDir) == False:
				os.makedirs(destDir)
			else:
				print "Can't create a directory for " + destPath + " because something that is not a directory exists at " + destDir
				sys.exit(3)
				print "Saving " + origPath + " -> " + destPath
		os.rename(origPath, destPath)
	
	def moveInvalidTo(self, backupPath):
		for mvFile in self.invalidList:
			if mvFile not in self.filesWithGood:
				origPath = os.path.join(dirpath, mvFile)
				destPath = os.path.join(backupPath, mvFile)
				self.backupFileTo(origPath, destPath)

		justFileList = list(fileElem[0] for fileElem in self.fileList)
		for dirpath, dirnames, filenames in os.walk(self.basePath):
			for filename in filenames:
				filePath = os.path.join(dirpath, filename)
				if filePath not in justFileList:
					destPath = os.path.join(backupPath, filename)
					self.backupFileTo(filePath, destPath)

	def purgeInvalid(self):
		for mvFile in self.invalidList:
			if mvFile not in self.filesWithGood:
				origPath = os.path.join(dirpath, mvFile)
				print "Deleting corrupt file " + origPath
				os.remove(origPath)

		justFileList = list(fileElem[0] for fileElem in self.fileList)
		for dirpath, dirnames, filenames in os.walk(self.basePath):
			for filename in filenames:
				filePath = os.path.join(dirpath, filename)
				if filePath not in justFileList:
					print "Deleting extra file " + filePath
					os.remove(filePath)

def usage():
	print "Usage :" + sys.argv[0] + " [-h] [-n] [-d] [-b <backupPath>] <torrentPath> <filesDir>"

if __name__ == "__main__":
	backupPath = None
	SHA1Check = True
	dryRun = False
	
	try:
		opts, args = getopt.getopt(sys.argv[1:], "b:dhn", ["help", "backup=", "nosha", "dryrun"])
	except getopt.GetoptError:
		usage()
		raise

	for opt, arg in opts:
		if opt in ("-h", "--help"):
			usage()
			sys.exit()
		elif opt in ("-b", "--backup"):
			backupPath = arg
			if os.path.isdir(backupPath) == False:
				print "Cannot us " + backupPath + " as a backup path"
				usage()
				sys.exit()
		elif opt in ("-d", "--dryrun"):
			dryRun = True
		elif opt in ("-n", "--nosha"):
			SHA1Check = False

	if len(args) < 2:
		print "Torrentfile and torrent dir not specified"
		usage()
		sys.exit(2)
		
	mainObj = PurgeDirCruftFromTorrent(args[0], args[1])

	if SHA1Check:
		mainObj.findInvalidFiles()

	if dryRun == True:
		print "Performing dryrun on " + args[1] + " using torrent at " + args[0]
		mainObj.listInvalid()
	else:
		if backupPath != None:
			print "Performing backup on " + args[1] + " using torrent at " + args[0] + " backing up to " + backupPath
			mainObj.moveInvalidTo(backupPath)
		else:
			print "Performing full delete on " + args[1] + " using torrent at " + args[0]
			mainObj.purgeInvalid()
