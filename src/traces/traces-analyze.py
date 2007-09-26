#!/usr/bin/python
# -*- coding: utf-8 -*-
# vim: fileencoding=utf-8 encoding=utf-8

from struct import *

import sys, os, mmap, stat, pprint, math, string
import psyco
import biggles

# -----------------------------------------------------------------------------

def classify(n):
	s = 8
	n = (n + (s - 1)) & ~(s - 1)

	while (1.0 - ((n - s) + 1.0) / n) < 1.0 / 32.0:
		s = s * 2
	
	return s

# -----------------------------------------------------------------------------

def prepareLog():
	log = {}

	i = 8
	s = 8

	while s < (1 << 28):
		frag = 1.0 - ((i - s) + 1.0) / i

		while frag < 1.0 / 32.0:
			if i & (s + s - 1) != 0:
				break

			s = s * 2
			
			frag = 1.0 - ((i - s) + 1.0) / i

		log[i] = 0

		i += s
	
	return log

# -----------------------------------------------------------------------------

def processLog(name):
	logfd  = os.open(name, os.O_RDONLY)
	loglen = int(os.fstat(logfd)[stat.ST_SIZE])
	data   = mmap.mmap(logfd, loglen, mmap.MAP_PRIVATE, mmap.PROT_READ)

	# === create traces per pid:threadid and block histogram ==================

	i   = 0
	num = 0

	blkhist = {}
	trace   = {}

	while i < loglen:
		# 0: uint32_t msec
		# 1: uint16_t opcode
		# 2: uint16_t pid
		# 3: uint32_t thrid
		# 4: uint32_t result
		# 5: uint32_t args[0]
		# 6: uint32_t args[1]

		logline = unpack("IHHIIII", data.read(24))

		id   = "%d:$%.8x" % (logline[2], logline[3])

		# blkhist
		if not blkhist.has_key(id):
			blkhist[id] = prepareLog()

		if logline[1] == 1:
			size = logline[5]
			bin  = classify(size)

			size = float((size + (bin - 1)) & ~(bin - 1))

			if not blkhist[id].has_key(size):
				blkhist[id][size] = 0.0

			blkhist[id][size] += 1.0

		# threads
		if not trace.has_key(id):
			trace[id] = []

		# free
		if logline[1] == 0:
			trace[id].append([logline[0], logline[1], logline[5]])

		# malloc
		if logline[1] == 1:
			trace[id].append([logline[0], logline[1], logline[5], logline[4]])

		# realloc
		if logline[1] == 2:
			trace[id].append([logline[0], logline[1], logline[5], logline[6], logline[4]])

		# memalign
		if logline[1] == 3:
			trace[id].append([logline[0], logline[1], logline[5], logline[6], logline[4]])

		i   += 24
		num += 1

	data.close()

	# =========================================================================

	blksize  = {}
	blkcount = {}

	for id in trace.keys():
		if not blksize.has_key(id):
			blksize[id] = {}

		if not blkcount.has_key(id):
			blkcount[id] = {}

		block = {}

		threshold = [1<<6, 1<<12, 1<<15, 1<<32]
		used  = [0, 0, 0, 0]
		count = [0, 0, 0, 0]

		blksize[id][0]  = [0, 0, 0, 0]
		blkcount[id][0] = [0, 0, 0, 0]

		for line in trace[id]:
			msec = line[0]

			if not blksize[id].has_key(msec):
				blksize[id][msec] = [ used[i] for i in range(4) ]

			if not blkcount[id].has_key(msec):
				blkcount[id][msec] = [ count[i] for i in range(4) ]

			# free
			if line[1] == 0:
				ptr = line[2]

				try:
					for i in range(4):
						if block[ptr] < threshold[i]:
							used[i] -= block[ptr]
							count[i] -= 1

					del block[ptr]
				except KeyError:
					print "Warning: block at address %x not found!" % ptr

			# malloc
			if line[1] == 1:
				size = line[2]
				res  = line[3]

				if block.has_key(res):
					print "Warning: block at address %x already exists!" % res

				block[res] = size

				for i in range(4):
					if size < threshold[i]:
						used[i]  += size
						count[i] += 1

			# realloc
			if line[1] == 2:
				ptr  = line[2]
				size = line[3]
				res  = line[4]

				if (ptr != res) and block.has_key(res):
					print "Warning: block at address %x already exists!" % res

				try:
					for i in range(4):
						if block[ptr] < threshold[i]:
							used[i]  -= block[ptr]
							count[i] -= 1

					del block[ptr]
				except KeyError:
					print "Warning: block at address %x not found!" % ptr
			
				block[res] = size

				for i in range(4):
					if size < threshold[i]:
						used[i]  += size
						count[i] += 1

			# memalign
			if line[1] == 3:
				align = line[2]
				size  = line[3]
				res   = line[4]

				if block.has_key(ptr):
					print "Warning: block at address %x already exists!" % res

				block[res] = size

				for i in range(4):
					if size < threshold[i]:
						used[i]  += size
						count[i] += 1

			# update !
			blksize[id][msec]  = [ used[i] for i in range(4) ]
			blkcount[id][msec] = [ count[i] for i in range(4) ]

	# =========================================================================

	return blkhist, blksize, blkcount

# -----------------------------------------------------------------------------

def drawBlkHist(blkhist):
	ts = blkhist.keys()
	ts.sort()

	for t in ts:
		# cut unnecessary sizes
		ss = blkhist[t].keys()
		ss.sort()

		while blkhist[t][ss[-1]] == 0:
			ss.pop()
		
		# calculate ranges & histogram
		xmax = ((len(ss) + 9) / 10) * 10
		ymax = 0

		ys = []

		for s in ss:
			if ymax < blkhist[t][s]:
				ymax = blkhist[t][s]

			y = blkhist[t][s]

			if y > 0:
				y = math.log(y, 10)

			ys.append(y)

		ymax = math.ceil(math.log(ymax, 10))

		# draw a plot
		p = biggles.FramedPlot()
		
		title = string.split(t, ":")

		p.title = "Analiza dla pid: %s threadid: 0x%s" % (title[0],title[1])

		p.aspect_ratio = 0.66

		p.xrange = (0, xmax)
		p.yrange = (0, ymax)

		p.x1.label = "rozmiar bloku"
		p.y1.label = "# allokacji"

		p.x1.tickdir = 1
		p.y1.tickdir = 1
		p.x2.draw_ticks = 0
		p.y2.draw_ticks = 0

		p.y1.ticks      = int(ymax) + 1
		p.y1.subticks   = 9
		p.y1.ticklabels = ["0"] + [ str(int(math.pow(10, i+1))) for i in range(int(ymax)) ]

		p.x1.draw_subticks = 0

		p.x1.ticks = [0]
		p.x1.ticklabels = ["$2^3$"]

		for n in range(len(ss)):
			if ss[n] < 64:
				continue

			v = math.log(ss[n], 2)

			if v == int(v):
				p.x1.ticks.append(n)
				p.x1.ticklabels.append("$2^{%d}$" % int(v))

		p.add(biggles.Histogram(ys, color = 0xFF0000))
		p.show()

# -----------------------------------------------------------------------------

def drawBlkSize(blksize):
	ks = blksize.keys()
	ks.sort()

	for k in ks:
		ts = blksize[k].keys()
		ts.sort()

		ys = [[], [], [], []]

		maxused = [0, 0, 0, 0]

		for i in range(4):
			for t in ts:
				ys[i].append(blksize[k][t][i])

				if maxused[i] < blksize[k][t][i]:
					maxused[i] = blksize[k][t][i]

		# draw a plot
		p = biggles.FramedPlot()

		p.xrange = (0, ts[-1])
		p.yrange = (0, maxused[-1])

		p.x1.label = "czas [ms]"
		p.y1.label = "bajty"
		
		p.aspect_ratio = 0.75
		p.add(biggles.FillBetween(ts, ys[2], ts, ys[3], color = 0x802080))	# all blocks
		p.add(biggles.FillBetween(ts, ys[1], ts, ys[2], color = 0xE038E0))	# blocks <= 32kiB
		p.add(biggles.FillBetween(ts, ys[0], ts, ys[1], color = 0x208080))	# blocks <= 4kiB
		p.add(biggles.FillBelow(ts, ys[0], color = 0x38E0E0))				# blocks <= 64B

		for i in range(4):
			p.add(biggles.Curve(ts, ys[i], color = 0x000000))

		p.show()

# -----------------------------------------------------------------------------

def drawBlkCount(blkcount):
	ks = blkcount.keys()
	ks.sort()

	for k in ks:
		ts = blkcount[k].keys()
		ts.sort()

		ys = [[], [], [], []]

		maxused = [0, 0, 0, 0]

		for i in range(4):
			for t in ts:
				ys[i].append(blkcount[k][t][i])

				if maxused[i] < blkcount[k][t][i]:
					maxused[i] = blkcount[k][t][i]

		# draw a plot
		p = biggles.FramedPlot()

		p.xrange = (0, ts[-1])
		p.yrange = (0, maxused[-1])

		p.x1.label = "czas [ms]"
		p.y1.label = "bloki"
		
		p.aspect_ratio = 0.75
		p.add(biggles.FillBetween(ts, ys[2], ts, ys[3], color = 0x802080))	# all blocks
		p.add(biggles.FillBetween(ts, ys[1], ts, ys[2], color = 0xE038E0))	# blocks <= 32kiB
		p.add(biggles.FillBetween(ts, ys[0], ts, ys[1], color = 0x208080))	# blocks <= 4kiB
		p.add(biggles.FillBelow(ts, ys[0], color = 0x38E0E0))				# blocks <= 64B

		for i in range(4):
			p.add(biggles.Curve(ts, ys[i], color = 0x000000))

		p.show()

# -----------------------------------------------------------------------------

psyco.full()

blkhist, blksize, blkcount = processLog(sys.argv[1])

drawBlkHist(blkhist)
drawBlkSize(blksize)
drawBlkCount(blkcount)
