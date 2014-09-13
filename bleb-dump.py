#!/usr/bin/env python3

import struct, sys

inputFilename = sys.argv[1]

ObjectEntryPrologueHeader_t = struct.Struct('<HHH')
RepositoryPrologue_t = struct.Struct('<7sBII')
SpanHeader_t = struct.Struct('<IIQ')
StreamDescriptor_t = struct.Struct('<QQ')

def align(value, alignment):
	return (int(value) + alignment - 1) & ~(alignment - 1)

def unpackStruct(struct, input):
	return struct.unpack(input.read(struct.size))

class RepositoryStream:
	def __init__(self, data):
		self.data = data
		self.pos = 0

	def eof(self):
		return self.pos >= len(self.data)

	def read(self, length=None):
		if length != None:
			data = self.data[self.pos:self.pos + length]
			self.pos += length
			return data
		else:
			return self.data

	def seek(self, amount, whence):
		if whence == 0:
			self.pos = amount
		else:
			self.pos += amount

with open(inputFilename, 'rb') as input:
	def readHeader():
		prologue = unpackStruct(RepositoryPrologue_t, input)

		if prologue[0] != b"\x89bleb\x0D\x0A":
			abort()

		print('Format version: %02Xh' % prologue[1])
		print('Flags: core=%08Xh info=%08Xh' % (prologue[2], prologue[3]))

	def getStreamContents(descr):
		location = descr[0]
		length = descr[1]
		data = b''

		input.seek(location, 0)

		while len(data) < length:
			header = unpackStruct(SpanHeader_t, input)
			#print(header)
			data += input.read(header[1])

			if len(data) < length:
				if header[2] == 0:
					abort

				input.seek(header[2], 0)

		return RepositoryStream(data)

	def printObjectEntryPrologueHeader(hdr):
		print('[length=%u, flags=%04X, nameLength=%u]' % (hdr[0] & 0x7fff, hdr[1], hdr[2]), end='')

	def printStreamDescriptor(descr):
		print('[location=%u, length=%u]' % descr, end='')

	def dumpContentDirectory(cdsDescr):
		print('Content Directory Stream:', end='\t')
		printStreamDescriptor(cdsDescr)
		print()

		dir = getStreamContents(cdsDescr)

		while not dir.eof():
			prologueHeader = unpackStruct(ObjectEntryPrologueHeader_t, dir)

			if (prologueHeader[0] & 0x8000) == 0:
				print('  Object', end='\t')
				printObjectEntryPrologueHeader(prologueHeader)
			else:
				print('  Deleted\t[length=%u]' % (prologueHeader[0] & 0x7fff))

				dir.seek(align(prologueHeader[0] & 0x7fff, 16) - ObjectEntryPrologueHeader_t.size, 1)
				continue

			name = dir.read(prologueHeader[2]).decode()
			print('\t`%s`' % name)

			IS_DIRECTORY = 0x0001
			HAS_STREAM_DESCR = 0x0002
			HAS_STORAGE_DESCR = 0x0004
			HAS_HASH128 = 0x0008
			HAS_INLINE_PAYLOAD = 0x0010
			IS_TEXT = 0x1001

			if prologueHeader[1] & HAS_STREAM_DESCR:
				streamDescr = unpackStruct(StreamDescriptor_t, dir)
				print('    Stream Descriptor', end='\t')
				printStreamDescriptor(streamDescr)
				print()

				if streamDescr[1] < 200:
					print(getStreamContents(streamDescr).read().decode('utf-8'))

			if prologueHeader[1] & HAS_INLINE_PAYLOAD:
				contents = dir.read(prologueHeader[0] - ObjectEntryPrologueHeader_t.size - prologueHeader[2]).decode()
				print('    Inline Payload:\t' + contents)

			padding = align(prologueHeader[0], 16) - prologueHeader[0]
			dir.seek(padding, 1)

	readHeader()
	cdsDescr = unpackStruct(StreamDescriptor_t, input)

	dumpContentDirectory(cdsDescr)

